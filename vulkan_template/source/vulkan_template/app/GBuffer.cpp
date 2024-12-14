#include "GBuffer.hpp"

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/app/Scene.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageOperations.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Pipeline.hpp"
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <cassert>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <utility>

namespace detail
{
auto allocateTextures(
    VkDevice const device, VmaAllocator const allocator, VkExtent2D const extent
) -> std::optional<vkt::GBufferTextures>
{
    std::optional<vkt::GBufferTextures> texturesResult{std::in_place};
    vkt::GBufferTextures& textures{texturesResult.value()};

    vkt::ImageAllocationParameters const imageParameters{
        .extent = extent,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usageFlags =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };

    // Give world position larger components to avoid precision issues
    vkt::ImageAllocationParameters const worldPositionParameters{
        .extent = extent,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usageFlags =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    vkt::ImageViewAllocationParameters const viewParameters{
        .subresourceRange =
            vkt::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT)
    };

    std::optional<std::unique_ptr<vkt::ImageView>> diffuseResult{
        vkt::ImageView::allocate(
            device, allocator, imageParameters, viewParameters
        )
    };
    if (!diffuseResult.has_value() || diffuseResult.value() == nullptr)
    {
        VKT_ERROR("Failed to create GBuffer diffuse color image.");
        return std::nullopt;
    }
    textures.diffuse = std::move(diffuseResult).value();

    std::optional<std::unique_ptr<vkt::ImageView>> specularResult{
        vkt::ImageView::allocate(
            device, allocator, imageParameters, viewParameters
        )
    };
    if (!specularResult.has_value() || specularResult.value() == nullptr)
    {
        VKT_ERROR("Failed to create GBuffer specular color image.");
        return std::nullopt;
    }
    textures.specular = std::move(specularResult).value();

    std::optional<std::unique_ptr<vkt::ImageView>> normalResult{
        vkt::ImageView::allocate(
            device, allocator, imageParameters, viewParameters
        )
    };
    if (!normalResult.has_value() || normalResult.value() == nullptr)
    {
        VKT_ERROR("Failed to create GBuffer normal image.");
        return std::nullopt;
    }
    textures.normal = std::move(normalResult).value();

    std::optional<std::unique_ptr<vkt::ImageView>> positionResult{
        vkt::ImageView::allocate(
            device, allocator, worldPositionParameters, viewParameters
        )
    };
    if (!positionResult.has_value() || positionResult.value() == nullptr)
    {
        VKT_ERROR("Failed to create GBuffer worldPosition image.");
        return std::nullopt;
    }
    textures.worldPosition = std::move(positionResult).value();

    std::optional<std::unique_ptr<vkt::ImageView>> ormResult{
        vkt::ImageView::allocate(
            device, allocator, imageParameters, viewParameters
        )
    };
    if (!ormResult.has_value() || ormResult.value() == nullptr)
    {
        VKT_ERROR("Failed to create GBuffer occlusionRoughnessMetallic image.");
        return std::nullopt;
    }
    textures.occlusionRoughnessMetallic = std::move(ormResult).value();

    return texturesResult;
}

// Can return a partial amount of samplers that need to be destroyed
auto allocateSamplers(VkDevice const device, size_t const count)
    -> std::vector<VkSampler>
{
    std::vector<VkSampler> samplers{};

    VkSamplerCreateInfo const samplerInfo{vkt::samplerCreateInfo(
        0,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VK_FILTER_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    )};

    for (size_t i{0}; i < count; i++)
    {
        VkSampler sampler;
        VKT_TRY_VK(
            vkCreateSampler(device, &samplerInfo, nullptr, &sampler),
            "Failed to create GBuffer immutable sampler.",
            samplers
        );
        samplers.push_back(sampler);
    }

    return samplers;
}

auto allocateWriteDescriptors(
    VkDevice const device,
    vkt::DescriptorAllocator& descriptorAllocator,
    VkDescriptorSetLayout const layout,
    vkt::GBufferTextures& textures,
    std::span<VkSampler const> const samplers
) -> std::optional<VkDescriptorSet>
{
    VkSampler const diffuseColorSampler{
        samplers[static_cast<size_t>(vkt::GBufferTextureIndices::Diffuse)]
    };
    VkSampler const specularColorSampler{
        samplers[static_cast<size_t>(vkt::GBufferTextureIndices::Specular)]
    };
    VkSampler const normalSampler{
        samplers[static_cast<size_t>(vkt::GBufferTextureIndices::Normal)]
    };
    VkSampler const positionSampler{
        samplers[static_cast<size_t>(vkt::GBufferTextureIndices::WorldPosition)]
    };
    VkSampler const ormSampler{
        samplers[static_cast<size_t>(vkt::GBufferTextureIndices::ORM)]
    };

    VkDescriptorSet const set{descriptorAllocator.allocate(device, layout)};

    std::vector<VkDescriptorImageInfo> const imageInfos{
        VkDescriptorImageInfo{
            .sampler = diffuseColorSampler,
            .imageView = textures.diffuse->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        },
        VkDescriptorImageInfo{
            .sampler = specularColorSampler,
            .imageView = textures.specular->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        },
        VkDescriptorImageInfo{
            .sampler = normalSampler,
            .imageView = textures.normal->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        },
        VkDescriptorImageInfo{
            .sampler = positionSampler,
            .imageView = textures.worldPosition->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        },
        VkDescriptorImageInfo{
            .sampler = ormSampler,
            .imageView = textures.occlusionRoughnessMetallic->view(),
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        }
    };

    VkWriteDescriptorSet const descriptorWrite{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,

        .dstSet = set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(imageInfos.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,

        .pImageInfo = imageInfos.data(),
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    std::vector<VkWriteDescriptorSet> const writes{descriptorWrite};
    vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);

    return set;
}

} // namespace detail

namespace vkt
{
auto GBuffer::operator=(GBuffer&& other) -> GBuffer&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);

    m_textures = std::exchange(other.m_textures, {});

    m_immutableSamplers = std::move(other.m_immutableSamplers);

    m_descriptorLayout =
        std::exchange(other.m_descriptorLayout, VK_NULL_HANDLE);
    m_descriptors = std::exchange(other.m_descriptors, VK_NULL_HANDLE);

    m_descriptorAllocator = std::move(other.m_descriptorAllocator);

    return *this;
}
GBuffer::GBuffer(GBuffer&& other) { *this = std::move(other); }
GBuffer::~GBuffer()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }

    m_descriptorAllocator.reset();
    vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);

    for (auto const& sampler : m_immutableSamplers)
    {
        vkDestroySampler(m_device, sampler, nullptr);
    }
}

auto GBuffer::create(
    VkDevice const device,
    VmaAllocator const allocator,
    VkExtent2D const capacity
) -> std::optional<GBuffer>
{
    std::optional<GBuffer> pipelineResult{std::in_place, GBuffer{}};
    GBuffer& pipeline{pipelineResult.value()};

    pipeline.m_device = device;

    auto texturesResult{detail::allocateTextures(device, allocator, capacity)};
    if (!texturesResult.has_value())
    {
        return std::nullopt;
    }
    pipeline.m_textures = std::move(texturesResult).value();

    pipeline.m_immutableSamplers =
        detail::allocateSamplers(device, GBUFFER_TEXTURE_COUNT);
    assert(
        pipeline.m_immutableSamplers.size() <= GBUFFER_TEXTURE_COUNT
        && "Allocated more samplers than necessary"
    );
    if (pipeline.m_immutableSamplers.size() != GBUFFER_TEXTURE_COUNT)
    {
        return std::nullopt;
    }

    std::vector<vkt::DescriptorAllocator::PoolSizeRatio> ratios{
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 1.0F}
    };
    pipeline.m_descriptorAllocator = std::make_unique<vkt::DescriptorAllocator>(
        vkt::DescriptorAllocator::create(device, 1, ratios, (VkFlags)0)
    );

    std::optional<VkDescriptorSetLayout> const descriptorLayoutResult{
        vkt::GBuffer::allocateDescriptorSetLayout(device)
    };
    if (!descriptorLayoutResult.has_value())
    {
        VKT_ERROR("Failed to create GBuffer descriptor set layout.");
        return std::nullopt;
    }
    pipeline.m_descriptorLayout = descriptorLayoutResult.value();

    auto descriptorsResult{detail::allocateWriteDescriptors(
        device,
        *pipeline.m_descriptorAllocator,
        pipeline.m_descriptorLayout,
        pipeline.m_textures,
        pipeline.m_immutableSamplers
    )};
    if (!descriptorsResult.has_value())
    {
        return std::nullopt;
    }
    pipeline.m_descriptors = descriptorsResult.value();

    return pipelineResult;
}

auto GBuffer::allocateDescriptorSetLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    // All descriptor bindings are the same, so we push instead of setting
    // individually

    DescriptorLayoutBuilder::BindingParams const bindingParams{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = 0,
    };

    DescriptorLayoutBuilder builder{};
    for (size_t i{0}; i < GBUFFER_TEXTURE_COUNT; i++)
    {
        builder.pushBinding(bindingParams);
    }

    std::optional<VkDescriptorSetLayout> const descriptorLayoutResult{
        builder.build(device, (VkFlags)0)
    };
    if (!descriptorLayoutResult.has_value())
    {
        VKT_ERROR("Failed to create GBuffer descriptor set layout.");
        return std::nullopt;
    }

    return descriptorLayoutResult;
}

auto GBuffer::capacity() const -> std::optional<VkExtent2D>
{
    if (m_textures.diffuse == nullptr)
    {
        return std::nullopt;
    }
    return m_textures.diffuse->image().extent2D();
}

void GBuffer::setSize(VkRect2D const size) { m_size = size; }

auto GBuffer::size() const -> VkRect2D { return m_size; }

void GBuffer::recordTransitionImages(
    VkCommandBuffer const cmd, VkImageLayout const dstLayout
) const
{
    m_textures.diffuse->recordTransitionBarriered(cmd, dstLayout);
    m_textures.specular->recordTransitionBarriered(cmd, dstLayout);
    m_textures.normal->recordTransitionBarriered(cmd, dstLayout);
    m_textures.worldPosition->recordTransitionBarriered(cmd, dstLayout);
    m_textures.occlusionRoughnessMetallic->recordTransitionBarriered(
        cmd, dstLayout
    );
}
auto GBuffer::attachmentInfo(VkImageLayout const layout) const -> std::array<
    VkRenderingAttachmentInfo,
    static_cast<size_t>(GBufferTextureIndices::COUNT)>
{
    std::array<
        VkRenderingAttachmentInfo,
        static_cast<size_t>(GBufferTextureIndices::COUNT)>
        infos{};

    infos[static_cast<size_t>(GBufferTextureIndices::Diffuse)] =
        renderingAttachmentInfo(m_textures.diffuse->view(), layout);
    infos[static_cast<size_t>(GBufferTextureIndices::Specular)] =
        renderingAttachmentInfo(m_textures.specular->view(), layout);
    infos[static_cast<size_t>(GBufferTextureIndices::Normal)] =
        renderingAttachmentInfo(m_textures.normal->view(), layout);
    infos[static_cast<size_t>(GBufferTextureIndices::WorldPosition)] =
        renderingAttachmentInfo(m_textures.worldPosition->view(), layout);
    infos[static_cast<size_t>(GBufferTextureIndices::ORM)] =
        renderingAttachmentInfo(
            m_textures.occlusionRoughnessMetallic->view(), layout
        );

    return infos;
};
} // namespace vkt

namespace detail
{
struct PushConstantVertex
{
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress modelBuffer;

    VkDeviceAddress modelInverseTransposeBuffer;
    // NOLINTNEXTLINE(modernize-avoid-c-arrays, readability-magic-numbers)
    uint8_t padding0[8];

    glm::mat4x4 cameraProjView;
};

// Set all the dynamic state required when using shader objects
void setRasterizationState(
    VkCommandBuffer const cmd,
    bool const reverseZ,
    VkRect2D const drawRect,
    size_t const colorAttachmentCount
)
{
    VkViewport const viewport{
        .x = static_cast<float>(drawRect.offset.x),
        .y = static_cast<float>(drawRect.offset.y),
        .width = static_cast<float>(drawRect.extent.width),
        .height = static_cast<float>(drawRect.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };

    vkCmdSetViewportWithCount(cmd, 1, &viewport);

    VkRect2D const scissor{drawRect};

    vkCmdSetScissorWithCount(cmd, 1, &scissor);

    vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);

    VkColorBlendEquationEXT const colorBlendEquation{};
    vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &colorBlendEquation);

    // No vertex input state since we use buffer addresses

    vkCmdSetCullModeEXT(cmd, VK_CULL_MODE_BACK_BIT);

    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
    vkCmdSetRasterizationSamplesEXT(cmd, VK_SAMPLE_COUNT_1_BIT);

    VkSampleMask const sampleMask{0b1};
    vkCmdSetSampleMaskEXT(cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);

    vkCmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);

    vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);

    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_CLOCKWISE);

    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);

    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOpEXT(
        cmd, reverseZ ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS
    );

    vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthBiasEnableEXT(cmd, VK_FALSE);

    vkCmdSetStencilTestEnable(cmd, VK_FALSE);

    VkColorComponentFlags const colorComponentFlags{
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkBool32 const colorBlendEnabled{VK_FALSE};
    std::vector<VkColorComponentFlags> attachmentWriteMasks{};
    std::vector<VkBool32> attachmentBlendEnabled{};

    for (size_t i{0}; i < colorAttachmentCount; i++)
    {
        attachmentWriteMasks.push_back(colorComponentFlags);
        attachmentBlendEnabled.push_back(colorBlendEnabled);
    }

    vkCmdSetColorWriteMaskEXT(cmd, 0, VKR_ARRAY(attachmentWriteMasks));
    vkCmdSetColorBlendEnableEXT(cmd, 0, VKR_ARRAY(attachmentBlendEnabled));
}

auto cameraProjView2(float const aspectRatio) -> glm::mat4x4
{
    glm::vec3 const translation{0.0F, 0.0F, -5.0F};
    glm::quat const orientation{glm::identity<glm::quat>()};
    float const swappedNear{10'000.0F};
    float const swappedFar{0.1F};

    float const fovRadians{glm::radians(70.0F)};

    // Use LH (opposite of our right handed) since we reverse depth
    glm::mat4x4 const projection{
        glm::perspectiveLH_ZO(fovRadians, aspectRatio, swappedNear, swappedFar)
    };
    glm::mat4x4 const view{
        glm::inverse(glm::translate(translation) * glm::toMat4(orientation))
    };

    return projection * view;
}
} // namespace detail

namespace vkt
{
auto GBufferPipeline::operator=(GBufferPipeline&& other) -> GBufferPipeline&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_fragmentStage = std::exchange(other.m_fragmentStage, VK_NULL_HANDLE);
    m_vertexStage = std::exchange(other.m_vertexStage, VK_NULL_HANDLE);

    m_graphicsLayout = std::exchange(other.m_graphicsLayout, VK_NULL_HANDLE);

    m_creationArguments = std::exchange(other.m_creationArguments, {});

    return *this;
}
GBufferPipeline::GBufferPipeline(GBufferPipeline&& other)
{
    *this = std::move(other);
}
GBufferPipeline::~GBufferPipeline()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_device, m_graphicsLayout, nullptr);
        vkDestroyShaderEXT(m_device, m_vertexStage, nullptr);
        vkDestroyShaderEXT(m_device, m_fragmentStage, nullptr);
    }
}
auto GBufferPipeline::create(
    VkDevice const device, RendererArguments const arguments
) -> std::optional<GBufferPipeline>
{
    std::optional<GBufferPipeline> result{std::in_place, GBufferPipeline{}};
    GBufferPipeline& pipeline{result.value()};
    pipeline.m_device = device;
    pipeline.m_creationArguments = arguments;

    std::filesystem::path const vertexPath{"shaders/deferred/gbuffer.vert.spv"};
    std::filesystem::path const fragmentPath{"shaders/deferred/gbuffer.frag.spv"
    };

    std::vector<VkDescriptorSetLayout> const layouts{};
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(detail::PushConstantVertex)
    }};

    std::optional<VkShaderEXT> const vertexShaderResult{vkt::loadShaderObject(
        device,
        vertexPath,
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        layouts,
        ranges,
        VkSpecializationInfo{}
    )};
    std::optional<VkShaderEXT> const fragmentShaderResult{vkt::loadShaderObject(
        device,
        fragmentPath,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        (VkFlags)0,
        layouts,
        ranges,
        VkSpecializationInfo{}
    )};
    if (!vertexShaderResult.has_value() || !fragmentShaderResult.has_value())
    {
        VKT_ERROR("Failed to compile shader.");
        return std::nullopt;
    }

    pipeline.m_fragmentStage = fragmentShaderResult.value();
    pipeline.m_vertexStage = vertexShaderResult.value();

    VkPipelineLayoutCreateInfo const layoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .setLayoutCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),

        .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    };

    if (VkResult const pipelineLayoutResult{vkCreatePipelineLayout(
            device, &layoutCreateInfo, nullptr, &pipeline.m_graphicsLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    return result;
}
void GBufferPipeline::recordDraw(
    VkCommandBuffer const cmd,
    RenderTarget& renderTarget,
    GBuffer& gbuffer,
    Scene& scene
)
{
    gbuffer.setSize(renderTarget.size());

    gbuffer.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    );

    detail::setRasterizationState(
        cmd,
        m_creationArguments.reverseZ,
        gbuffer.size(),
        static_cast<size_t>(GBufferTextureIndices::COUNT)
    );
    auto const gbufferAttachments{
        gbuffer.attachmentInfo(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    };

    VkRenderingAttachmentInfo const depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,

        .imageView = renderTarget.depth().view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,

        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,

        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

        .clearValue{VkClearValue{.depthStencil{.depth = 0.0F}}},
    };

    VkRenderingInfo const renderInfo{
        renderingInfo(gbuffer.size(), gbufferAttachments, &depthAttachment)
    };

    std::array<VkShaderStageFlagBits, 2> const stages{
        VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT
    };
    std::array<VkShaderEXT, 2> const shaders{m_vertexStage, m_fragmentStage};

    vkCmdBeginRendering(cmd, &renderInfo);

    VkClearValue const clearColor{.color{.float32{0.0, 0.0, 0.0, 0.0}}};
    std::vector<VkClearAttachment> clearAttachmentInfos{};
    for (uint32_t i{0}; i < static_cast<size_t>(GBufferTextureIndices::COUNT);
         i++)
    {
        clearAttachmentInfos.push_back(VkClearAttachment{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = i,
            .clearValue = clearColor,
        });
    }
    VkClearRect const clearRect{
        .rect = gbuffer.size(),
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vkCmdClearAttachments(cmd, VKR_ARRAY(clearAttachmentInfos), 1, &clearRect);

    assert(stages.size() == shaders.size());
    vkCmdBindShadersEXT(cmd, stages.size(), stages.data(), shaders.data());

    {
        auto const aspectRatio{static_cast<float>(
            vkt::aspectRatio(renderTarget.size().extent).value()
        )};
        glm::mat4x4 const cameraProjView{detail::cameraProjView2(aspectRatio)};

        MeshBuffers& meshBuffers{*scene.mesh->meshBuffers};

        detail::PushConstantVertex const vertexPushConstant{
            .vertexBuffer = meshBuffers.vertexAddress(),
            .modelBuffer = scene.models->deviceAddress(),
            .modelInverseTransposeBuffer =
                scene.modelInverseTransposes->deviceAddress(),
            .cameraProjView = cameraProjView,
        };
        vkCmdPushConstants(
            cmd,
            m_graphicsLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(detail::PushConstantVertex),
            &vertexPushConstant
        );

        vkCmdBindIndexBuffer(
            cmd, meshBuffers.indexBuffer(), 0, VK_INDEX_TYPE_UINT32
        );

        VkDeviceSize const instanceCount{scene.models->deviceSize()};
        for (GeometrySurface const& surface : scene.mesh->surfaces)
        {
            vkCmdDrawIndexed(
                cmd, surface.indexCount, instanceCount, surface.firstIndex, 0, 0
            );
        }
    }

    std::array<VkShaderStageFlagBits, 2> const unboundStages{
        VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT
    };
    std::array<VkShaderEXT, 2> const unboundHandles{
        VK_NULL_HANDLE, VK_NULL_HANDLE
    };
    vkCmdBindShadersEXT(cmd, VKR_ARRAY(unboundStages), unboundHandles.data());

    vkCmdEndRendering(cmd);
}
} // namespace vkt