#include "RenderTarget.hpp"

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <array>
#include <imgui.h>
#include <span>
#include <utility>
#include <vector>

namespace vkt
{
RenderTarget::RenderTarget(RenderTarget&& other) noexcept
{
    *this = std::move(other);
}

auto RenderTarget::operator=(RenderTarget&& other) noexcept -> RenderTarget&
{
    destroy();

    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);

    m_descriptorPool = std::move(other.m_descriptorPool);

    m_colorSampler = std::exchange(other.m_colorSampler, VK_NULL_HANDLE);
    m_color = std::move(other.m_color);

    m_depthSampler = std::exchange(other.m_depthSampler, VK_NULL_HANDLE);
    m_depth = std::move(other.m_depth);

    m_singletonDescriptorLayout =
        std::exchange(other.m_singletonDescriptorLayout, VK_NULL_HANDLE);
    m_singletonDescriptor =
        std::exchange(other.m_singletonDescriptor, VK_NULL_HANDLE);

    m_combinedDescriptorLayout =
        std::exchange(other.m_combinedDescriptorLayout, VK_NULL_HANDLE);
    m_combinedDescriptor =
        std::exchange(other.m_combinedDescriptor, VK_NULL_HANDLE);

    return *this;
}

RenderTarget::~RenderTarget() { destroy(); }

auto RenderTarget::create(
    VkDevice const device,
    VmaAllocator const allocator,
    CreateParameters const parameters
) -> std::optional<RenderTarget>
{
    if (ImGui::GetIO().BackendRendererUserData == nullptr)
    {
        VKT_ERROR("ImGui backend not initialized.");
        return std::nullopt;
    }

    std::optional<RenderTarget> result{RenderTarget{}};
    RenderTarget& renderTarget{result.value()};
    renderTarget.m_device = device;

    std::array<DescriptorAllocator::PoolSizeRatio, 2> poolRatios{
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1.0F,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1.0F,
    };
    renderTarget.m_descriptorPool =
        std::make_unique<DescriptorAllocator>(DescriptorAllocator::create(
            device, 4, poolRatios, static_cast<VkFlags>(0)
        ));

    VkImageUsageFlags const colorUsage{
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT // used as descriptor for e.g. ImGui
        | VK_IMAGE_USAGE_STORAGE_BIT // used in compute passes
        | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT // used in graphics passes
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT     // copy into
    };

    if (std::optional<std::unique_ptr<ImageView>> colorResult{
            ImageView::allocate(
                device,
                allocator,
                ImageAllocationParameters{
                    .extent = parameters.max,
                    .format = parameters.color,
                    .usageFlags = colorUsage,
                },
                ImageViewAllocationParameters{}
            )
        };
        colorResult.has_value() && colorResult.value() != nullptr)
    {
        renderTarget.m_color = std::move(colorResult).value();

        VkSamplerCreateInfo const samplerInfo{samplerCreateInfo(
            0,
            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            VK_FILTER_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
        )};

        VkSampler sampler{VK_NULL_HANDLE};
        VKT_TRY_VK(
            vkCreateSampler(
                device, &samplerInfo, nullptr, &renderTarget.m_colorSampler
            ),
            "Failed to allocate sampler.",
            std::nullopt
        );
    }
    else
    {
        VKT_ERROR("Failed to allocate color image.");
        return std::nullopt;
    }

    VkImageUsageFlags const depthUsage{
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
    };

    if (std::optional<std::unique_ptr<ImageView>> depthResult{
            ImageView::allocate(
                device,
                allocator,
                ImageAllocationParameters{
                    .extent = parameters.max,
                    .format = parameters.depth,
                    .usageFlags = depthUsage,
                },
                ImageViewAllocationParameters{
                    .subresourceRange =
                        imageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
                }
            )
        };
        depthResult.has_value() && depthResult.value() != nullptr)
    {
        renderTarget.m_depth = std::move(depthResult).value();

        VkSamplerCreateInfo const samplerInfo{samplerCreateInfo(
            0,
            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            VK_FILTER_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
        )};

        VKT_TRY_VK(
            vkCreateSampler(
                device, &samplerInfo, nullptr, &renderTarget.m_depthSampler
            ),
            "Failed to allocate sampler.",
            std::nullopt
        );
    }
    else
    {
        VKT_ERROR("Failed to allocate color image.");
        return std::nullopt;
    }

    if (auto const singletonResult{allocateSingletonLayout(device)};
        singletonResult.has_value())
    {
        renderTarget.m_singletonDescriptorLayout = singletonResult.value();
        renderTarget.m_singletonDescriptor =
            renderTarget.m_descriptorPool->allocate(
                device, renderTarget.m_singletonDescriptorLayout
            );
    }
    else
    {
        VKT_ERROR("Failed to allocate singleton descriptor layout.");
        return std::nullopt;
    }

    if (auto const combinedResult{allocateCombinedLayout(device)};
        combinedResult.has_value())
    {
        renderTarget.m_combinedDescriptorLayout = combinedResult.value();
        renderTarget.m_combinedDescriptor =
            renderTarget.m_descriptorPool->allocate(
                device, renderTarget.m_combinedDescriptorLayout
            );
    }
    else
    {
        VKT_ERROR("Failed to allocate combined descriptor layout.");
        return std::nullopt;
    }

    {
        VkDescriptorImageInfo const colorInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = renderTarget.m_color->view(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkDescriptorImageInfo const depthInfo{
            .sampler = renderTarget.m_depthSampler,
            .imageView = renderTarget.m_depth->view(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet const singletonColorWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,

            .dstSet = renderTarget.m_singletonDescriptor,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,

            .pImageInfo = &colorInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        VkWriteDescriptorSet const combinedColorWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,

            .dstSet = renderTarget.m_combinedDescriptor,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,

            .pImageInfo = &colorInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        VkWriteDescriptorSet const combinedDepthWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,

            .dstSet = renderTarget.m_combinedDescriptor,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,

            .pImageInfo = &depthInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        std::vector<VkWriteDescriptorSet> const writes{
            singletonColorWrite, combinedColorWrite, combinedDepthWrite
        };

        vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);
    }

    return result;
}

auto RenderTarget::allocateSingletonLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    auto const layoutResult{
        DescriptorLayoutBuilder{}
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .build(device, 0)
    };

    if (!layoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate singleton descriptor layout.");
        return std::nullopt;
    }

    return layoutResult.value();
}

auto RenderTarget::allocateCombinedLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    auto const layoutResult{
        DescriptorLayoutBuilder{}
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .build(device, 0)
    };

    if (!layoutResult.has_value())
    {

        VKT_ERROR("Failed to allocate combined descriptor layout.");
        return std::nullopt;
    }
    return layoutResult.value();
}

auto RenderTarget::colorSampler() const -> VkSampler { return m_colorSampler; }

auto RenderTarget::color() -> ImageView& { return *m_color; }

auto RenderTarget::color() const -> ImageView const& { return *m_color; }

auto RenderTarget::depth() -> ImageView& { return *m_depth; }

auto RenderTarget::depth() const -> ImageView const& { return *m_depth; }

auto RenderTarget::singletonDescriptor() const -> VkDescriptorSet
{
    return m_singletonDescriptor;
}

auto RenderTarget::singletonLayout() const -> VkDescriptorSetLayout
{
    return m_singletonDescriptorLayout;
}

auto RenderTarget::combinedDescriptor() const -> VkDescriptorSet
{
    return m_combinedDescriptor;
}

auto RenderTarget::combinedDescriptorLayout() const -> VkDescriptorSetLayout
{
    return m_combinedDescriptorLayout;
}

void RenderTarget::setSize(VkRect2D const size) { m_size = size; }

auto RenderTarget::size() const -> VkRect2D { return m_size; }

void RenderTarget::destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(
            m_device, m_singletonDescriptorLayout, nullptr
        );
        vkDestroyDescriptorSetLayout(
            m_device, m_combinedDescriptorLayout, nullptr
        );
        vkDestroySampler(m_device, m_colorSampler, nullptr);
        vkDestroySampler(m_device, m_depthSampler, nullptr);
    }

    m_descriptorPool.reset();

    m_singletonDescriptorLayout = VK_NULL_HANDLE;
    m_singletonDescriptor = VK_NULL_HANDLE;

    m_combinedDescriptorLayout = VK_NULL_HANDLE;
    m_combinedDescriptor = VK_NULL_HANDLE;

    m_color.reset();

    m_colorSampler = VK_NULL_HANDLE;

    m_depth.reset();

    m_depthSampler = VK_NULL_HANDLE;

    m_device = VK_NULL_HANDLE;
}
} // namespace vkt