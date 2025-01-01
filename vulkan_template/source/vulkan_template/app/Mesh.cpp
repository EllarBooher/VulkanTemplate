#include "Mesh.hpp"

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <cassert>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace vkt
{
auto MeshBuffers::uploadMeshData(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue const& submissionQueue,
    std::span<uint32_t const> const indices,
    std::span<VertexPacked const> const vertices
) -> std::optional<MeshBuffers>
{
    std::optional<MeshBuffers> result{std::in_place, MeshBuffers{}};
    MeshBuffers& buffers{result.value()};

    size_t const indexBufferSize{indices.size_bytes()};
    size_t const vertexBufferSize{vertices.size_bytes()};

    buffers.m_indexBuffer =
        std::make_unique<AllocatedBuffer>(AllocatedBuffer::allocate(
            device,
            allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        ));

    buffers.m_vertexBuffer =
        std::make_unique<AllocatedBuffer>(AllocatedBuffer::allocate(
            device,
            allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        ));

    AllocatedBuffer stagingBuffer{AllocatedBuffer::allocate(
        device,
        allocator,
        vertexBufferSize + indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY,
        VMA_ALLOCATION_CREATE_MAPPED_BIT
    )};

    assert(
        stagingBuffer.isMapped()
        && "Staging buffer for mesh upload was not mapped."
    );

    stagingBuffer.writeBytes(
        0,
        std::span<uint8_t const>{
            reinterpret_cast<uint8_t const*>(vertices.data()), vertexBufferSize
        }
    );
    stagingBuffer.writeBytes(
        vertexBufferSize,
        std::span<uint8_t const>{
            reinterpret_cast<uint8_t const*>(indices.data()), indexBufferSize
        }
    );

    if (auto result{submissionQueue.immediateSubmit(
            [&](VkCommandBuffer cmd)
    {
        VkBufferCopy const vertexCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = vertexBufferSize,
        };
        vkCmdCopyBuffer(
            cmd, stagingBuffer.buffer(), buffers.vertexBuffer(), 1, &vertexCopy
        );

        VkBufferCopy const indexCopy{
            .srcOffset = vertexBufferSize,
            .dstOffset = 0,
            .size = indexBufferSize,
        };
        vkCmdCopyBuffer(
            cmd, stagingBuffer.buffer(), buffers.indexBuffer(), 1, &indexCopy
        );
    }
        )};
        result != ImmediateSubmissionQueue::SubmissionResult::SUCCESS)
    {
        VKT_ERROR("Vertex/Index buffer submission failed.");
        return std::nullopt;
    }

    return result;
}

auto MaterialDescriptorPool::operator=(MaterialDescriptorPool&& other) noexcept
    -> MaterialDescriptorPool&
{
    device = std::exchange(other.device, VK_NULL_HANDLE);
    materialSampler = std::exchange(other.materialSampler, VK_NULL_HANDLE);
    materialLayout = std::exchange(other.materialLayout, VK_NULL_HANDLE);

    materialAllocator = std::exchange(other.materialAllocator, nullptr);

    return *this;
}

MaterialDescriptorPool::MaterialDescriptorPool(MaterialDescriptorPool&& other
) noexcept
{
    *this = std::move(other);
}

auto MaterialDescriptorPool::create(VkDevice const device)
    -> std::optional<MaterialDescriptorPool>
{
    std::optional<MaterialDescriptorPool> poolResult{
        std::in_place, MaterialDescriptorPool{}
    };
    MaterialDescriptorPool& pool{poolResult.value()};
    pool.device = device;

    // Max number of unique surfaces on meshes this pool will be able to support
    uint32_t constexpr MAX_SETS{100};
    std::vector<vkt::DescriptorAllocator::PoolSizeRatio> const
        materialPoolRatios{
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 1.0F}
        };
    pool.materialAllocator = std::make_unique<vkt::DescriptorAllocator>(
        vkt::DescriptorAllocator::create(
            device, MAX_SETS, materialPoolRatios, (VkFlags)0
        )
    );
    auto layoutResult{allocateMaterialDescriptorLayout(device)};
    if (!layoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate Material descriptor set layout.");
        return std::nullopt;
    }
    pool.materialLayout = layoutResult.value();

    VkSamplerCreateInfo const samplerInfo{vkt::samplerCreateInfo(
        static_cast<VkFlags>(0),
        VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    )};

    // Assert since failing would mean leaking a descriptor set
    VKT_TRY_VK(
        vkCreateSampler(device, &samplerInfo, nullptr, &pool.materialSampler),
        "Failed to allocate Material sampler.",
        std::nullopt
    );

    return poolResult;
}

void MaterialDescriptorPool::fillMaterial(MaterialMaps& material)
{
    assert(
        material.descriptor == VK_NULL_HANDLE
        && "Material already has descriptor"
    );

    material.descriptor = materialAllocator->allocate(device, materialLayout);

    std::vector<VkDescriptorImageInfo> bindings{
        VkDescriptorImageInfo{
            .sampler = materialSampler,
            .imageView = material.color->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        },
        VkDescriptorImageInfo{
            .sampler = materialSampler,
            .imageView = material.normal->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        }
    };
    VkWriteDescriptorSet const write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,

        .dstSet = material.descriptor,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(bindings.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,

        .pImageInfo = bindings.data(),
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    std::vector<VkWriteDescriptorSet> writes{write};

    vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);
}

auto MaterialDescriptorPool::allocateMaterialDescriptorLayout(
    VkDevice const device
) -> std::optional<VkDescriptorSetLayout>
{
    // Color & normal
    auto const layoutResult{
        DescriptorLayoutBuilder{}
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stageMask =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .bindingFlags = 0,
            })
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stageMask =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .bindingFlags = 0,
            })
            .build(device, 0)
    };

    if (!layoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate material descriptor layout.");
        return std::nullopt;
    }

    return layoutResult.value();
}
MaterialDescriptorPool::~MaterialDescriptorPool()
{
    if (device != VK_NULL_HANDLE)
    {
        materialAllocator.reset();
        vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
        vkDestroySampler(device, materialSampler, nullptr);
    }
}
} // namespace vkt