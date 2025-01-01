#include "LightingPass.hpp"

#include "vulkan_template/app/GBuffer.hpp"
#include "vulkan_template/app/PropertyTable.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/app/Scene.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/core/UIWindowScope.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageOperations.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanOverloads.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <cassert>
#include <filesystem>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
struct ShadowMapPushConstant
{
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress modelBuffer;

    glm::mat4x4 cameraProjView;
};
static_assert(std::is_standard_layout_v<ShadowMapPushConstant>);
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(ShadowMapPushConstant) == 80ULL);

struct LightingPassPushConstant
{
    glm::vec2 offset;
    glm::vec2 gBufferCapacity;

    glm::vec4 cameraPosition;

    glm::vec4 lightForward;

    glm::mat4 cameraProjView;

    glm::vec2 extent;
    float lightStrength;
    float ambientStrength;

    glm::vec2 padding0;
    float shadowReceiverPlaneDepthBias;
    uint32_t gbufferWhiteOverride;

    glm::mat4x4 lightProjView;

    glm::uvec2 shadowMapSize;
    float shadowReceiverConstantBias;
    uint32_t enableShadows;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(LightingPassPushConstant) == 224ULL);

struct LightingPassSpecializationConstant
{
    uint32_t enableAO{VK_FALSE};
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(LightingPassSpecializationConstant) == 4ULL);

struct SSAOPassPushConstant
{
    glm::vec2 offset;
    glm::vec2 gBufferCapacity;

    glm::vec4 cameraPosition;

    glm::vec2 extent;
    float occluderRadius;
    float occluderBias;

    float aoScale;
    glm::vec3 padding0;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(SSAOPassPushConstant) == 64ULL);

struct SSAOPassSpecializationConstant
{
    uint32_t enableRandomNormalSampling{VK_FALSE};
    uint32_t normalizeRandomNormals{VK_FALSE};
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(SSAOPassSpecializationConstant) == 8ULL);
} // namespace

template <> struct std::hash<::LightingPassSpecializationConstant>
{
    auto operator()(::LightingPassSpecializationConstant const& sc
    ) const noexcept -> size_t
    {
        // A bit silly since these represent bools, but more general is better
        size_t const h1{std::hash<uint32_t>{}(sc.enableAO)};
        return h1;
    }
};

template <> struct std::hash<::SSAOPassSpecializationConstant>
{
    auto operator()(::SSAOPassSpecializationConstant const& sc) const noexcept
        -> size_t
    {
        // A bit silly since these represent bools, but more general is better
        size_t const h1{std::hash<uint32_t>{}(sc.enableRandomNormalSampling)};
        size_t const h2{std::hash<uint32_t>{}(sc.normalizeRandomNormals)};
        return h1 ^ (h2 << 1);
    }
};

namespace
{
auto computeLightProjView(
    vkt::LightingPassParameters const& parameters, bool const reverseZ
)
{
    auto const X{glm::angleAxis(
        parameters.lightAxisAngles.x, glm::vec3{1.0F, 0.0F, 0.0F}
    )};
    auto const Y{glm::angleAxis(
        parameters.lightAxisAngles.y, glm::vec3{0.0F, 1.0F, 0.0F}
    )};
    auto const Z{glm::angleAxis(
        parameters.lightAxisAngles.z, glm::vec3{0.0F, 0.0F, 1.0F}
    )};
    glm::quat const lightOrientation{Z * X * Y};
    glm::vec3 const lightForward{
        glm::toMat4(Z * X * Y) * glm::vec4(0.0, 0.0, 1.0, 0.0)
    };

    float near{parameters.shadowNearPlane};
    float far{parameters.shadowFarPlane};
    if (reverseZ)
    {
        std::swap(near, far);
    }

    // TODO: compute light projection matrix based on camera frustrum/scene size

    glm::vec3 const min{-parameters.lightProjViewExtent, near};
    glm::vec3 const max{parameters.lightProjViewExtent, far};

    glm::mat4x4 const projection{
        glm::orthoLH_ZO(min.x, max.x, min.y, max.y, min.z, max.z)
    };
    glm::mat4x4 const view{glm::inverse(
        glm::translate(-5.0F * lightForward) * glm::toMat4(lightOrientation)
    )};

    return projection * view;
}

auto allocateRandomNormalsLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    VkDescriptorSetLayoutCreateFlags const flags{0};
    return vkt::DescriptorLayoutBuilder{}
        .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
            .bindingFlags = 0,
        })
        .build(device, flags);
}
auto allocateAOLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    VkDescriptorSetLayoutCreateFlags const flags{0};
    return vkt::DescriptorLayoutBuilder{}
        .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
            .bindingFlags = 0,
        })
        .build(device, flags);
}

auto createRandomNormalsImage(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue
) -> std::optional<vkt::ImageView>
{
    size_t constexpr DEFAULT_IMAGE_DIMENSIONS{256ULL};
    VkFormat constexpr FORMAT{VK_FORMAT_R16G16_SNORM};

    vkt::ImageRG16_SNORM rawImage{
        .extent = glm::u32vec2{DEFAULT_IMAGE_DIMENSIONS},
        .texels = std::vector<vkt::TexelRG16_SNORM>(
            DEFAULT_IMAGE_DIMENSIONS * DEFAULT_IMAGE_DIMENSIONS
        )
    };

    glm::vec3 const defaultNormal{0.0F, 0.0F, 1.0F};
    float constexpr JUST_UNDER_MAX_INT_16{32767.9F};

    for (vkt::TexelRG16_SNORM& texel : rawImage.texels)
    {
        glm::vec3 const deflectedNormal{
            glm::normalize(defaultNormal + glm::ballRand(1.0F))
        };
        // pack the normal
        // int16_t stores -256 to 255
        // normal components run from -1 to 1

        texel.r = static_cast<int16_t>(
            glm::floor(deflectedNormal.x * JUST_UNDER_MAX_INT_16)
        );
        texel.g = static_cast<int16_t>(
            glm::floor(deflectedNormal.y * JUST_UNDER_MAX_INT_16)
        );
    }

    auto imageResult{vkt::Image::uploadToDevice(
        device,
        allocator,
        submissionQueue,
        FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT,
        rawImage.extent,
        std::span<uint8_t const>{
            reinterpret_cast<uint8_t const*>(rawImage.texels.data()),
            rawImage.texels.size()
                * sizeof(decltype(rawImage.texels)::value_type)
        }
    )};
    if (!imageResult.has_value())
    {
        VKT_ERROR("Failed to upload random normals image.");
        return std::nullopt;
    }

    submissionQueue.immediateSubmit(
        [&](VkCommandBuffer cmd)
    {
        imageResult.value().recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
    );

    return vkt::ImageView::allocate(
        device,
        allocator,
        std::move(imageResult).value(),
        vkt::ImageViewAllocationParameters{}
    );
}

auto fillSingleComputeImageDescriptor(
    VkDevice const device,
    vkt::ImageView& imageView,
    vkt::DescriptorAllocator& descriptorAllocator,
    VkDescriptorSetLayout const layout
) -> std::optional<VkDescriptorSet>
{
    std::optional<VkDescriptorSet> setResult{VK_NULL_HANDLE};
    VkDescriptorSet& set{setResult.value()};
    VkDescriptorType constexpr DESCRIPTOR_TYPE{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };

    set = descriptorAllocator.allocate(device, layout);

    std::vector<VkDescriptorImageInfo> bindings{VkDescriptorImageInfo{
        .sampler = VK_NULL_HANDLE,
        .imageView = imageView.view(),
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    }};
    VkWriteDescriptorSet const write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,

        .dstSet = set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(bindings.size()),
        .descriptorType = DESCRIPTOR_TYPE,

        .pImageInfo = bindings.data(),
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    std::vector<VkWriteDescriptorSet> writes{write};

    vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);

    return setResult;
}

auto fillInputOutputComputeDescriptor(
    VkDevice const device,
    vkt::ImageView& inputImage,
    vkt::ImageView& outputImage,
    vkt::DescriptorAllocator& descriptorAllocator,
    VkDescriptorSetLayout const layout
) -> std::optional<VkDescriptorSet>
{
    std::optional<VkDescriptorSet> setResult{VK_NULL_HANDLE};
    VkDescriptorSet& set{setResult.value()};
    VkDescriptorType constexpr DESCRIPTOR_TYPE{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };

    set = descriptorAllocator.allocate(device, layout);

    std::vector<VkDescriptorImageInfo> bindings{
        {
            .sampler = VK_NULL_HANDLE,
            .imageView = inputImage.view(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .sampler = VK_NULL_HANDLE,
            .imageView = outputImage.view(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        }
    };
    VkWriteDescriptorSet const write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,

        .dstSet = set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(bindings.size()),
        .descriptorType = DESCRIPTOR_TYPE,

        .pImageInfo = bindings.data(),
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    std::vector<VkWriteDescriptorSet> writes{write};

    vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);

    return setResult;
}

auto createShadowmappingPassResources(VkDevice const device)
    -> std::optional<vkt::ShadowmappingPassResources>
{
    std::optional<vkt::ShadowmappingPassResources> resourcesResult{
        std::in_place, vkt::ShadowmappingPassResources{}
    };
    vkt::ShadowmappingPassResources& resources{resourcesResult.value()};

    char const* const OFFSCREEN_VERTEX_SHADER_PATH{
        "shaders/shadowmapping/offscreen.vert.spv"
    };

    { // shader
        std::vector<VkDescriptorSetLayout> const layouts{};
        std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(ShadowMapPushConstant),
        }};

        std::optional<VkShaderEXT> const shaderResult{vkt::loadShaderObject(
            device,
            OFFSCREEN_VERTEX_SHADER_PATH,
            VK_SHADER_STAGE_VERTEX_BIT,
            (VkShaderStageFlagBits)0,
            layouts,
            ranges,
            VkSpecializationInfo{}
        )};
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shadowmap shader.");
            return std::nullopt;
        }
        resources.vertexShader = shaderResult.value();

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
                device,
                &layoutCreateInfo,
                nullptr,
                &resources.vertexShaderLayout
            )};
            pipelineLayoutResult != VK_SUCCESS)
        {
            VKT_LOG_VK(
                pipelineLayoutResult,
                "Failed to create shadowmap pipeline layout."
            );
            return std::nullopt;
        }
    }

    return resourcesResult;
}

auto createLightingPassResources(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::DescriptorAllocator& descriptorAllocator
) -> std::optional<vkt::LightingPassResources>
{
    std::optional<vkt::LightingPassResources> resourcesResult{
        std::in_place, vkt::LightingPassResources{}
    };
    vkt::LightingPassResources& resources{resourcesResult.value()};

    auto const renderTargetLayoutResult{
        vkt::RenderTarget::allocateSingletonLayout(device)
    };
    if (!renderTargetLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate render target descriptor set layout.");
        return std::nullopt;
    }
    resources.renderTargetLayout = renderTargetLayoutResult.value();

    auto const gbufferLayoutResult{
        vkt::GBuffer::allocateDescriptorSetLayout(device)
    };
    if (!gbufferLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    resources.gbufferLayout = gbufferLayoutResult.value();

    auto const randomNormalsLayoutResult{::allocateRandomNormalsLayout(device)};
    if (!randomNormalsLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    resources.inputAOLayout = randomNormalsLayoutResult.value();

    { // combined sampler
        VkExtent2D constexpr SHADOWMAP_EXTENT{8192U, 8192U};

        VkSamplerCreateInfo const samplerInfo{vkt::samplerCreateInfo(
            0,
            VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            VK_FILTER_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
        )};

        VkResult const samplerResult{vkCreateSampler(
            device, &samplerInfo, nullptr, &resources.shadowMapSampler
        )};

        if (samplerResult != VK_SUCCESS)
        {
            VKT_LOG_VK(samplerResult, "Creating Shadow Pass Sampler");
            return std::nullopt;
        }

        std::optional<VkDescriptorSetLayout> buildResult{
            vkt::DescriptorLayoutBuilder{}
                .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                    .bindingFlags = 0,
                })
                .build(device, 0)
        };
        if (!buildResult.has_value())
        {
            VKT_ERROR("Unable to build shadowmap sampler descriptor layout.");
            return std::nullopt;
        }
        resources.shadowMapSetLayout = buildResult.value();
        resources.shadowMapSet =
            descriptorAllocator.allocate(device, resources.shadowMapSetLayout);

        std::optional<vkt::ImageView> shadowmapResult{vkt::ImageView::allocate(
            device,
            allocator,
            vkt::ImageAllocationParameters{
                .extent = SHADOWMAP_EXTENT,
                .format = VK_FORMAT_D32_SFLOAT,
                .usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                            | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            },
            vkt::ImageViewAllocationParameters{
                .subresourceRange =
                    vkt::imageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            }
        )};
        if (!shadowmapResult.has_value())
        {
            VKT_ERROR("Unable to allocate shadowmap texture.");
            return {};
        }
        resources.shadowMap =
            std::make_unique<vkt::ImageView>(std::move(shadowmapResult).value()
            );

        VkDescriptorImageInfo const combinedSamplerInfo{
            .sampler = resources.shadowMapSampler,
            .imageView = resources.shadowMap->view(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet const writeInfo{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,

            .dstSet = resources.shadowMapSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,

            .pImageInfo = &combinedSamplerInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        std::vector<VkWriteDescriptorSet> const writes{writeInfo};
        vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);
    }

    std::vector<VkDescriptorSetLayout> const layouts{
        resources.renderTargetLayout,
        resources.gbufferLayout,
        resources.inputAOLayout,
        resources.shadowMapSetLayout
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(::LightingPassPushConstant),
    }};

    std::vector<VkSpecializationMapEntry> const specializationEntries{
        {.constantID = 0,
         .offset = offsetof(LightingPassSpecializationConstant, enableAO),
         .size = sizeof(LightingPassSpecializationConstant::enableAO)}
    };

    std::vector<LightingPassSpecializationConstant> const
        specializationConstants{{VK_FALSE}, {VK_TRUE}};

    char const* SHADER_PATH{"shaders/deferred/light.comp.spv"};
    for (auto const& specialization : specializationConstants)
    {
        VkSpecializationInfo const specializationInfo{
            VKR_ARRAY(specializationEntries),
            sizeof(LightingPassSpecializationConstant),
            &specialization
        };
        std::optional<VkShaderEXT> const shaderResult = vkt::loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            specializationInfo
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        size_t const hash{std::hash<LightingPassSpecializationConstant>{
        }(specialization)};
        resources.shaderBySpecializationHash[hash] = shaderResult.value();
    }

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
            device, &layoutCreateInfo, nullptr, &resources.shaderLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    return resourcesResult;
}

auto createSSAOPassResources(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::DescriptorAllocator& descriptorAllocator,
    vkt::ImmediateSubmissionQueue const& imageUploadQueue
) -> std::optional<vkt::SSAOPassResources>
{
    std::optional<vkt::SSAOPassResources> resourcesResult{
        std::in_place, vkt::SSAOPassResources{}
    };
    vkt::SSAOPassResources& resources{resourcesResult.value()};

    auto const outputAOLayoutResult{::allocateAOLayout(device)};
    if (!outputAOLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate render target descriptor set layout.");
        return std::nullopt;
    }
    resources.outputAOLayout = outputAOLayoutResult.value();

    auto const gbufferOccludeeLayoutResult{
        vkt::GBuffer::allocateDescriptorSetLayout(device)
    };
    if (!gbufferOccludeeLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    resources.gbufferOccludeeLayout = gbufferOccludeeLayoutResult.value();

    auto const gbufferOccluderLayoutResult{
        vkt::GBuffer::allocateDescriptorSetLayout(device)
    };
    if (!gbufferOccluderLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    resources.gbufferOccluderLayout = gbufferOccluderLayoutResult.value();

    auto const randomNormalsLayoutResult{::allocateRandomNormalsLayout(device)};
    if (!randomNormalsLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    resources.randomNormalsLayout = randomNormalsLayoutResult.value();

    std::vector<VkDescriptorSetLayout> const layouts{
        resources.outputAOLayout,
        resources.gbufferOccludeeLayout,
        resources.randomNormalsLayout,
        resources.gbufferOccluderLayout,
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(::SSAOPassPushConstant),
    }};

    std::vector<VkSpecializationMapEntry> const specializationEntries{
        {.constantID = 0,
         .offset = offsetof(
             SSAOPassSpecializationConstant, enableRandomNormalSampling
         ),
         .size =
             sizeof(SSAOPassSpecializationConstant::enableRandomNormalSampling)
        },
        {.constantID = 1,
         .offset =
             offsetof(SSAOPassSpecializationConstant, normalizeRandomNormals),
         .size = sizeof(SSAOPassSpecializationConstant::normalizeRandomNormals)}
    };

    std::vector<SSAOPassSpecializationConstant> const specializationConstants{
        {VK_FALSE, VK_FALSE},
        {VK_FALSE, VK_TRUE},
        {VK_TRUE, VK_FALSE},
        {VK_TRUE, VK_TRUE}
    };

    char const* SHADER_PATH{"shaders/deferred/ssao.comp.spv"};
    for (auto const& specialization : specializationConstants)
    {
        VkSpecializationInfo const specializationInfo{
            VKR_ARRAY(specializationEntries),
            sizeof(SSAOPassSpecializationConstant),
            &specialization
        };
        std::optional<VkShaderEXT> const shaderResult = vkt::loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            specializationInfo
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        size_t const hash{std::hash<SSAOPassSpecializationConstant>{
        }(specialization)};
        resources.shaderBySpecializationHash[hash] = shaderResult.value();
    }

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
            device, &layoutCreateInfo, nullptr, &resources.shaderLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    auto randomNormalsResult{
        ::createRandomNormalsImage(device, allocator, imageUploadQueue)
    };
    if (!randomNormalsResult.has_value())
    {
        VKT_ERROR(
            "Failed to create random normals image for Lighting Pass pipeline."
        );
        return std::nullopt;
    }
    resources.randomNormals =
        std::make_unique<vkt::ImageView>(std::move(randomNormalsResult).value()
        );

    auto randomNormalsSetResult{::fillSingleComputeImageDescriptor(
        device,
        *resources.randomNormals,
        descriptorAllocator,
        resources.randomNormalsLayout
    )};
    if (!randomNormalsSetResult.has_value())
    {
        VKT_ERROR("Failed to fill random normals descriptor set for Lighting "
                  "Pass pipeline.");
        return std::nullopt;
    }
    resources.randomNormalsSet = randomNormalsSetResult.value();

    // Must be same size as gbuffer
    VkExtent2D constexpr TEXTURE_MAX{4096, 4096};

    // STORAGE for compute load/store
    // TRANSFER_DST for clear
    // TRANSFER_SRC for copying to output texture for visualization purposes
    VkImageUsageFlags constexpr AO_USAGE{
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    };
    auto ambientOcclusionResult{vkt::ImageView::allocate(
        device,
        allocator,
        vkt::ImageAllocationParameters{
            .extent = TEXTURE_MAX,
            .format = VK_FORMAT_R16_UNORM,
            .usageFlags = AO_USAGE,
        },
        vkt::ImageViewAllocationParameters{}
    )};
    if (!ambientOcclusionResult.has_value())
    {
        VKT_ERROR("Failed to create ambient occlusion image.");
        return std::nullopt;
    }
    resources.ambientOcclusion = std::make_unique<vkt::ImageView>(
        std::move(ambientOcclusionResult).value()
    );

    auto ambientOcclusionSetResult{::fillSingleComputeImageDescriptor(
        device,
        *resources.ambientOcclusion,
        descriptorAllocator,
        resources.outputAOLayout
    )};
    if (!ambientOcclusionSetResult.has_value())
    {
        VKT_ERROR("Failed to fill ambient occlusion output set for Lighting "
                  "Pass pipeline.");
        return std::nullopt;
    }
    resources.ambientOcclusionSet = ambientOcclusionSetResult.value();

    return resourcesResult;
}

// InputImage is needed to write it into the input/output descriptor set
auto createGaussianBlurPassResources(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::DescriptorAllocator& descriptorAllocator,
    vkt::ImageView& inputImage
) -> std::optional<vkt::GaussianBlurPassResources>
{
    std::optional<vkt::GaussianBlurPassResources> resourcesResult{
        std::in_place, vkt::GaussianBlurPassResources{}
    };
    vkt::GaussianBlurPassResources& resources{resourcesResult.value()};

    auto const inputOutputLayoutResult{
        vkt::DescriptorLayoutBuilder{}
            .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .build(device, (VkDescriptorSetLayoutCreateFlags)0)
    };
    if (!inputOutputLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate Input/Output image set layout.");
        return std::nullopt;
    }
    resources.inputOutputLayout = inputOutputLayoutResult.value();

    std::vector<VkDescriptorSetLayout> const layouts{resources.inputOutputLayout
    };
    std::vector<VkPushConstantRange> const ranges{};

    {
        char const* SHADER_PATH{
            "shaders/gaussian_blur/gaussian_blur.vertical.comp.spv"
        };
        auto const shaderResult = vkt::loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            VkSpecializationInfo{}
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        resources.verticalBlurShader = shaderResult.value();
    }

    {
        char const* SHADER_PATH{
            "shaders/gaussian_blur/gaussian_blur.horizontal.comp.spv"
        };
        auto const shaderResult = vkt::loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            VkSpecializationInfo{}
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        resources.horizontalBlurShader = shaderResult.value();
    }

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
            device, &layoutCreateInfo, nullptr, &resources.blurLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    {
        auto outputImageResult{vkt::ImageView::allocate(
            device,
            allocator,
            inputImage.image().allocationParameters(),
            inputImage.allocationParameters()
        )};
        if (!outputImageResult.has_value())
        {
            VKT_ERROR("Failed to create gaussian blur output image.");
            return std::nullopt;
        }
        resources.halfBlurredImage = std::make_unique<vkt::ImageView>(
            std::move(outputImageResult).value()
        );
    }
    {
        auto outputImageResult{vkt::ImageView::allocate(
            device,
            allocator,
            inputImage.image().allocationParameters(),
            inputImage.allocationParameters()
        )};
        if (!outputImageResult.has_value())
        {
            VKT_ERROR("Failed to create gaussian blur output image.");
            return std::nullopt;
        }
        resources.fullyBlurredImage = std::make_unique<vkt::ImageView>(
            std::move(outputImageResult).value()
        );
    }

    {
        auto inputOutputSetResult{::fillInputOutputComputeDescriptor(
            device,
            inputImage,
            *resources.halfBlurredImage,
            descriptorAllocator,
            resources.inputOutputLayout
        )};
        if (!inputOutputSetResult.has_value())
        {
            VKT_ERROR("Failed to fill input output image set for gaussian blur."
            );
            return std::nullopt;
        }
        resources.verticalBlurInputOutputSet = inputOutputSetResult.value();
    }
    {
        auto inputOutputSetResult{::fillInputOutputComputeDescriptor(
            device,
            *resources.halfBlurredImage,
            *resources.fullyBlurredImage,
            descriptorAllocator,
            resources.inputOutputLayout
        )};
        if (!inputOutputSetResult.has_value())
        {
            VKT_ERROR("Failed to fill input output image set for gaussian blur."
            );
            return std::nullopt;
        }
        resources.horizontalBlurInputOutputSet = inputOutputSetResult.value();
    }

    // Create a descriptor layout/set for binding this output image alone to a
    // compute shader
    auto const outputImageLayoutResult{
        vkt::DescriptorLayoutBuilder{}
            .pushBinding(vkt::DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindingFlags = 0,
            })
            .build(device, (VkDescriptorSetLayoutCreateFlags)0)
    };
    if (!outputImageLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate render target descriptor set layout.");
        return std::nullopt;
    }
    resources.fullyBlurredImageLayout = outputImageLayoutResult.value();

    auto outputSetResult{::fillSingleComputeImageDescriptor(
        device,
        *resources.fullyBlurredImage,
        descriptorAllocator,
        resources.fullyBlurredImageLayout
    )};
    if (!outputSetResult.has_value())
    {
        VKT_ERROR("Failed to allocate output image set.");
        return std::nullopt;
    }
    resources.fullyBlurredImageSet = outputSetResult.value();

    return resourcesResult;
}

void cleanResources(
    VkDevice const device, vkt::LightingPassResources& resources
)
{
    vkDestroyDescriptorSetLayout(device, resources.renderTargetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, resources.gbufferLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, resources.inputAOLayout, nullptr);

    vkDestroySampler(device, resources.shadowMapSampler, nullptr);
    vkDestroyDescriptorSetLayout(device, resources.shadowMapSetLayout, nullptr);

    for (auto const& [hash, shader] : resources.shaderBySpecializationHash)
    {
        vkDestroyShaderEXT(device, shader, nullptr);
    }

    vkDestroyPipelineLayout(device, resources.shaderLayout, nullptr);
}

void cleanResources(VkDevice const device, vkt::SSAOPassResources& resources)
{
    vkDestroyDescriptorSetLayout(device, resources.outputAOLayout, nullptr);

    vkDestroyDescriptorSetLayout(
        device, resources.gbufferOccludeeLayout, nullptr
    );
    vkDestroyDescriptorSetLayout(
        device, resources.gbufferOccluderLayout, nullptr
    );

    vkDestroyDescriptorSetLayout(
        device, resources.randomNormalsLayout, nullptr
    );

    for (auto const& [hash, shader] : resources.shaderBySpecializationHash)
    {
        vkDestroyShaderEXT(device, shader, nullptr);
    }

    vkDestroyPipelineLayout(device, resources.shaderLayout, nullptr);
}

void cleanResources(
    VkDevice const device, vkt::GaussianBlurPassResources& resources
)
{
    vkDestroyDescriptorSetLayout(device, resources.inputOutputLayout, nullptr);
    vkDestroyDescriptorSetLayout(
        device, resources.fullyBlurredImageLayout, nullptr
    );

    vkDestroyShaderEXT(device, resources.verticalBlurShader, nullptr);
    vkDestroyShaderEXT(device, resources.horizontalBlurShader, nullptr);

    vkDestroyPipelineLayout(device, resources.blurLayout, nullptr);
}

void cleanResources(
    VkDevice const device, vkt::ShadowmappingPassResources& resources
)
{
    vkDestroyShaderEXT(device, resources.vertexShader, nullptr);
    vkDestroyPipelineLayout(device, resources.vertexShaderLayout, nullptr);
}

} // namespace

namespace vkt
{
// NOLINTBEGIN(readability-magic-numbers)
LightingPassParameters LightingPass::DEFAULT_PARAMETERS{

    .lightAxisAngles = glm::vec3{0.0F, 1.3F, 0.8F},
    .lightStrength = 10.0F,
    .ambientStrength = 0.1F,

    .gbufferWhiteOverride = true,

    .enableAOFromFrontFace = true,
    .enableAOFromBackFace = true,
    .enableRandomNormalSampling = true,
    .normalizeRandomNormals = false,
    .occluderRadius = 1.0F,
    .occluderBias = 0.05F,
    .aoScale = 0.03F,

    .blurAOTexture = true,

    .enableShadows = true,

    .shadowReceiverPlaneDepthBias = 60.0F,
    .shadowReceiverConstantBias = 0.001F,

    .depthBiasConstant = 0.0F,
    .depthBiasSlope = -2.0F,

    .lightProjViewExtent = glm::vec2{10.0F, 10.F},
    .shadowNearPlane = -10.0F,
    .shadowFarPlane = 10.0F,
};
// NOLINTEND(readability-magic-numbers)

auto LightingPass::operator=(LightingPass&& other) -> LightingPass&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_descriptorAllocator = std::exchange(other.m_descriptorAllocator, nullptr);

    m_shadowmappingPassResources =
        std::exchange(other.m_shadowmappingPassResources, {});
    m_lightingPassResources = std::exchange(other.m_lightingPassResources, {});
    m_ssaoPassResources = std::exchange(other.m_ssaoPassResources, {});
    m_gaussianBlurPassResources =
        std::exchange(other.m_gaussianBlurPassResources, {});

    return *this;
}
LightingPass::LightingPass(LightingPass&& other) noexcept
{
    *this = std::move(other);
}
LightingPass::~LightingPass()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }

    ::cleanResources(m_device, m_shadowmappingPassResources);
    ::cleanResources(m_device, m_lightingPassResources);
    ::cleanResources(m_device, m_ssaoPassResources);
    ::cleanResources(m_device, m_gaussianBlurPassResources);
}
auto LightingPass::create(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue const& submissionQueue
) -> std::optional<LightingPass>
{ // Lighting pass pipeline

    std::optional<LightingPass> lightingPassResult{
        std::in_place, LightingPass{}
    };
    LightingPass& lightingPass{lightingPassResult.value()};
    lightingPass.m_device = device;

    size_t constexpr MAX_SETS{10ULL};
    std::vector<DescriptorAllocator::PoolSizeRatio> POOL_RATIOS{{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .ratio = 1.0F,
    }};
    auto descriptorAllocatorResult{
        DescriptorAllocator::create(device, MAX_SETS, POOL_RATIOS, (VkFlags)0)
    };
    // if (!descriptorAllocatorResult.has_value())
    // {
    //     VKT_ERROR("Failed to create descriptor allocator for Lighting "
    //               "Pass pipeline.");
    //     return std::nullopt;
    // }
    lightingPass.m_descriptorAllocator = std::make_unique<DescriptorAllocator>(
        std::move(descriptorAllocatorResult)
    );

    auto shadowmappingPassResourcesResult{
        ::createShadowmappingPassResources(lightingPass.m_device)
    };
    if (!shadowmappingPassResourcesResult.has_value())
    {
        VKT_ERROR("Failed to create shadowmapping pass resources.");
        return std::nullopt;
    }
    lightingPass.m_shadowmappingPassResources =
        std::move(shadowmappingPassResourcesResult).value();

    auto lightingPassResourcesResult{::createLightingPassResources(
        lightingPass.m_device, allocator, *lightingPass.m_descriptorAllocator
    )};
    if (!lightingPassResourcesResult.has_value())
    {
        VKT_ERROR("Failed to create lighting pass resources.");
        return std::nullopt;
    }
    lightingPass.m_lightingPassResources =
        std::move(lightingPassResourcesResult).value();

    auto ssaoPassResourcesResult{::createSSAOPassResources(
        lightingPass.m_device,
        allocator,
        *lightingPass.m_descriptorAllocator,
        submissionQueue
    )};
    if (!ssaoPassResourcesResult.has_value())
    {
        VKT_ERROR("Failed to create ssao pass resources.");
        return std::nullopt;
    }
    lightingPass.m_ssaoPassResources =
        std::move(ssaoPassResourcesResult).value();

    auto gaussianBlurPassResourcesResult{::createGaussianBlurPassResources(
        lightingPass.m_device,
        allocator,
        *lightingPass.m_descriptorAllocator,
        *lightingPass.m_ssaoPassResources.ambientOcclusion
    )};
    if (!gaussianBlurPassResourcesResult.has_value())
    {
        VKT_ERROR("Failed to create gaussian blur pass resources.");
        return std::nullopt;
    }
    lightingPass.m_gaussianBlurPassResources =
        std::move(gaussianBlurPassResourcesResult).value();

    return lightingPassResult;
}
} // namespace vkt

namespace
{

// Set all the dynamic state required when using graphics shader objects
void setRasterizationState(
    VkCommandBuffer const cmd,
    bool const reverseZ,
    VkRect2D const drawRect,
    size_t const colorAttachmentCount,
    bool const backface,
    vkt::LightingPassParameters const& parameters
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

    vkCmdSetCullModeEXT(
        cmd, backface ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT
    );

    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
    vkCmdSetRasterizationSamplesEXT(cmd, VK_SAMPLE_COUNT_1_BIT);

    VkSampleMask const sampleMask{0b1};
    vkCmdSetSampleMaskEXT(cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);

    vkCmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);

    vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);

    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);

    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);

    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOpEXT(
        cmd, reverseZ ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS
    );

    vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthBiasEnableEXT(cmd, VK_TRUE);
    VkDepthBiasInfoEXT const depthBias{
        .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
        .depthBiasConstantFactor = parameters.depthBiasConstant,
        .depthBiasSlopeFactor = parameters.depthBiasSlope,
    };
    vkCmdSetDepthBias2EXT(cmd, &depthBias);

    vkCmdSetStencilTestEnable(cmd, VK_FALSE);

    if (colorAttachmentCount > 0)
    {
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
}

void recordDrawShadowmap(
    VkCommandBuffer const cmd,
    vkt::ShadowmappingPassResources& resources,
    vkt::ImageView& shadowMap,
    vkt::LightingPassParameters const& parameters,
    vkt::Scene const& scene
)
{
    shadowMap.recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
    );

    bool constexpr REVERSE_Z{true};
    VkRect2D const drawRect{
        .offset = VkOffset2D{0, 0}, .extent = shadowMap.image().extent2D()
    };

    setRasterizationState(cmd, REVERSE_Z, drawRect, 0, false, parameters);

    VkRenderingAttachmentInfo const depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,

        .imageView = shadowMap.view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,

        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,

        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

        .clearValue =
            VkClearValue{.depthStencil{.depth = REVERSE_Z ? 0.0F : 1.0F}},
    };

    VkExtent2D const depthExtent{shadowMap.image().extent2D()};

    VkRenderingInfo const renderInfo{vkt::renderingInfo(
        VkRect2D{.extent = depthExtent}, {}, &depthAttachment
    )};

    vkCmdBeginRendering(cmd, &renderInfo);

    VkShaderStageFlagBits constexpr VERTEX_STAGE{VK_SHADER_STAGE_VERTEX_BIT};
    vkCmdBindShadersEXT(cmd, 1, &VERTEX_STAGE, &resources.vertexShader);

    vkt::InstanceRenderingInfo const sceneRenderInfo{
        scene.instanceRenderingInfo()
    };
    for (auto const& instanceSpan : sceneRenderInfo.instances)
    {
        vkt::MeshBuffers& meshBuffers{*instanceSpan.mesh.get().meshBuffers};

        ShadowMapPushConstant const vertexPushConstant{
            .vertexBuffer = meshBuffers.vertexAddress(),
            .modelBuffer = sceneRenderInfo.models,
            .cameraProjView = computeLightProjView(parameters, REVERSE_Z),
        };
        vkCmdPushConstants(
            cmd,
            resources.vertexShaderLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(ShadowMapPushConstant),
            &vertexPushConstant
        );

        vkCmdBindIndexBuffer(
            cmd, meshBuffers.indexBuffer(), 0, VK_INDEX_TYPE_UINT32
        );

        for (auto const& surface : instanceSpan.mesh.get().surfaces)
        {
            vkCmdDrawIndexed(
                cmd,
                surface.indexCount,
                instanceSpan.count,
                surface.firstIndex,
                0,
                instanceSpan.start
            );
        }
    }

    VkShaderStageFlagBits const unboundStages{VK_SHADER_STAGE_VERTEX_BIT};
    VkShaderEXT const unboundHandles{VK_NULL_HANDLE};
    vkCmdBindShadersEXT(cmd, 1, &unboundStages, &unboundHandles);

    vkCmdEndRendering(cmd);
}

void recordDrawSSAO(
    VkCommandBuffer const cmd,
    vkt::GBuffer const& occludee,
    vkt::GBuffer const& occluder,
    vkt::SSAOPassResources& resources,
    vkt::LightingPassParameters const& parameters,
    vkt::Scene const& scene
)
{
    // GBuffers must be same size since shader uses the same UV between them
    assert(occludee.capacity() == occluder.capacity());
    assert(occludee.size() == occluder.size());

    uint32_t constexpr WORKGROUP_SIZE{16};
    VkExtent2D const gBufferCapacity{occludee.capacity().value()};

    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    std::vector<VkDescriptorSet> descriptors{
        resources.ambientOcclusionSet,
        occludee.descriptor(),
        resources.randomNormalsSet,
        occluder.descriptor()
    };

    resources.ambientOcclusion->recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_GENERAL
    );

    occludee.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    occluder.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    SSAOPassSpecializationConstant const specializationConstant{
        .enableRandomNormalSampling =
            parameters.enableRandomNormalSampling ? VK_TRUE : VK_FALSE,
        .normalizeRandomNormals =
            parameters.normalizeRandomNormals ? VK_TRUE : VK_FALSE,
    };

    VkShaderEXT const shader{resources.shaderBySpecializationHash.at(
        std::hash<SSAOPassSpecializationConstant>{}(specializationConstant)
    )};
    vkCmdBindShadersEXT(cmd, 1, &stage, &shader);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        resources.shaderLayout,
        0,
        VKR_ARRAY(descriptors),
        VKR_ARRAY_NONE
    );

    VkRect2D const drawRect{occludee.size()};

    SSAOPassPushConstant const pc{
        .offset =
            glm::vec2{
                static_cast<float>(drawRect.offset.x),
                static_cast<float>(drawRect.offset.y)
            },
        .gBufferCapacity =
            glm::vec2{
                static_cast<float>(gBufferCapacity.width),
                static_cast<float>(gBufferCapacity.height)
            },
        .cameraPosition = glm::vec4{scene.camera().translation, 1.0F},
        .extent =
            glm::vec2{
                static_cast<float>(drawRect.extent.width),
                static_cast<float>(drawRect.extent.height)
            },
        .occluderRadius = parameters.occluderRadius,
        .occluderBias = parameters.occluderBias,
        .aoScale = parameters.aoScale,
    };

    vkCmdPushConstants(
        cmd,
        resources.shaderLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc
    );

    vkt::computeDispatch(
        cmd,
        VkExtent3D{drawRect.extent.width, drawRect.extent.height, 1},
        VkExtent3D{
            .width = WORKGROUP_SIZE, .height = WORKGROUP_SIZE, .depth = 1
        }
    );
}

void recordBlurAO(
    VkCommandBuffer const cmd,
    vkt::ImageView& inputImage,
    vkt::GaussianBlurPassResources& resources
)
{
    inputImage.recordTransitionBarriered(cmd, VK_IMAGE_LAYOUT_GENERAL);
    resources.fullyBlurredImage->recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_GENERAL
    );
    resources.halfBlurredImage->recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_GENERAL
    );

    VkClearColorValue const clearColor{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
    resources.fullyBlurredImage->image().recordClearEntireColor(
        cmd, &clearColor
    );
    resources.halfBlurredImage->image().recordClearEntireColor(
        cmd, &clearColor
    );

    uint32_t constexpr WORKGROUP_WIDTH{32};

    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};

    {
        std::vector<VkDescriptorSet> descriptors{
            resources.verticalBlurInputOutputSet
        };
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            resources.blurLayout,
            0,
            VKR_ARRAY(descriptors),
            VKR_ARRAY_NONE
        );

        vkCmdBindShadersEXT(cmd, 1, &stage, &resources.verticalBlurShader);

        VkExtent3D const verticalGlobalSize{
            .width = inputImage.image().extent3D().width,
            .height = 1,
            .depth = 1
        };
        VkExtent3D constexpr VERTICAL_LOCAL_SIZE{
            .width = 32, .height = 1, .depth = 1
        };

        vkt::computeDispatch(cmd, verticalGlobalSize, VERTICAL_LOCAL_SIZE);
    }

    {
        std::vector<VkDescriptorSet> descriptors{
            resources.horizontalBlurInputOutputSet
        };
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            resources.blurLayout,
            0,
            VKR_ARRAY(descriptors),
            VKR_ARRAY_NONE
        );

        vkCmdBindShadersEXT(cmd, 1, &stage, &resources.horizontalBlurShader);

        VkExtent3D const horizontalGlobalSize{
            .width = 1,
            .height = inputImage.image().extent3D().height,
            .depth = 1
        };
        VkExtent3D constexpr HORIZONTAL_LOCAL_SIZE{
            .width = 1, .height = 32, .depth = 1
        };

        vkt::computeDispatch(cmd, horizontalGlobalSize, HORIZONTAL_LOCAL_SIZE);
    }

    VkShaderEXT constexpr NULL_SHADER{VK_NULL_HANDLE};
    vkCmdBindShadersEXT(cmd, 1, &stage, &NULL_SHADER);
}

void recordDrawLighting(
    VkCommandBuffer const cmd,
    vkt::RenderTarget& outputTexture,
    vkt::GBuffer const& gbuffer,
    VkDescriptorSet const ambientOcclusion,
    vkt::LightingPassResources& resources,
    vkt::LightingPassParameters const& parameters,
    vkt::Scene const& scene
)
{
    uint32_t constexpr WORKGROUP_SIZE{16};
    VkExtent2D const gBufferCapacity{gbuffer.capacity().value()};
    VkRect2D const drawRect{outputTexture.size()};
    auto const aspectRatio{
        static_cast<float>(vkt::aspectRatio(drawRect.extent).value())
    };

    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    std::vector<VkDescriptorSet> descriptors{
        outputTexture.singletonDescriptor(),
        gbuffer.descriptor(),
        ambientOcclusion,
        resources.shadowMapSet
    };

    outputTexture.color().recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_GENERAL
    );

    VkClearColorValue const clearColor{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
    outputTexture.color().image().recordClearEntireColor(cmd, &clearColor);

    gbuffer.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    resources.shadowMap->recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
    );

    bool const enableAO{
        parameters.enableAOFromFrontFace || parameters.enableAOFromBackFace
    };
    LightingPassSpecializationConstant const specializationConstant{
        .enableAO = enableAO ? VK_TRUE : VK_FALSE,
    };
    VkShaderEXT const shader{resources.shaderBySpecializationHash.at(
        std::hash<LightingPassSpecializationConstant>{}(specializationConstant)
    )};
    vkCmdBindShadersEXT(cmd, 1, &stage, &shader);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        resources.shaderLayout,
        0,
        VKR_ARRAY(descriptors),
        VKR_ARRAY_NONE
    );

    auto const X{glm::angleAxis(
        parameters.lightAxisAngles.x, glm::vec3{1.0F, 0.0F, 0.0F}
    )};
    auto const Y{glm::angleAxis(
        parameters.lightAxisAngles.y, glm::vec3{0.0F, 1.0F, 0.0F}
    )};
    auto const Z{glm::angleAxis(
        parameters.lightAxisAngles.z, glm::vec3{0.0F, 0.0F, 1.0F}
    )};

    // glm::quat const lightOrientation{Z * X * Y};
    glm::vec3 const lightForward{
        glm::toMat4(Z * X * Y) * glm::vec4(0.0, 0.0, 1.0, 0.0)
    };

    bool constexpr REVERSE_Z{true};
    VkExtent2D const shadowMapSize{resources.shadowMap->image().extent2D()};

    LightingPassPushConstant const pc{
        .offset =
            glm::vec2{
                static_cast<float>(drawRect.offset.x),
                static_cast<float>(drawRect.offset.y)
            },
        .gBufferCapacity =
            glm::vec2{
                static_cast<float>(gBufferCapacity.width),
                static_cast<float>(gBufferCapacity.height)
            },
        .cameraPosition = glm::vec4{scene.camera().translation, 1.0F},
        .lightForward = glm::vec4{lightForward, 0.0},
        .cameraProjView = scene.cameraProjView(aspectRatio),
        .extent =
            glm::vec2{
                static_cast<float>(drawRect.extent.width),
                static_cast<float>(drawRect.extent.height)
            },
        .lightStrength = parameters.lightStrength,
        .ambientStrength = parameters.ambientStrength,
        .shadowReceiverPlaneDepthBias = parameters.shadowReceiverPlaneDepthBias,
        .gbufferWhiteOverride =
            parameters.gbufferWhiteOverride ? VK_TRUE : VK_FALSE,
        .lightProjView = computeLightProjView(parameters, REVERSE_Z),
        .shadowMapSize = glm::uvec2{shadowMapSize.width, shadowMapSize.height},
        .shadowReceiverConstantBias = parameters.shadowReceiverConstantBias,
        .enableShadows = parameters.enableShadows ? VK_TRUE : VK_FALSE,
    };

    vkCmdPushConstants(
        cmd,
        resources.shaderLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc
    );

    vkt::computeDispatch(
        cmd,
        VkExtent3D{drawRect.extent.width, drawRect.extent.height, 1},
        VkExtent3D{
            .width = WORKGROUP_SIZE, .height = WORKGROUP_SIZE, .depth = 1
        }
    );
}

} // namespace

namespace vkt
{
void LightingPass::recordDraw(
    VkCommandBuffer const cmd,
    RenderTarget& texture,
    GBuffer const& frontFace,
    GBuffer const& backFace,
    Scene const& scene
)
{
    assert(
        texture.size() == frontFace.size() && texture.size() == backFace.size()
        && "GBuffer and render target must be same size."
    );

    m_ssaoPassResources.ambientOcclusion->recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_GENERAL
    );
    // Start with no attenuation due to AO
    VkClearColorValue const clearColor{.float32 = {1.0F, 1.0F, 1.0F, 1.0F}};
    m_ssaoPassResources.ambientOcclusion->image().recordClearEntireColor(
        cmd, &clearColor
    );

    if (m_parameters.enableAOFromFrontFace)
    {
        recordDrawSSAO(
            cmd, frontFace, frontFace, m_ssaoPassResources, m_parameters, scene
        );
    }
    if (m_parameters.enableAOFromBackFace)
    {
        recordDrawSSAO(
            cmd, frontFace, backFace, m_ssaoPassResources, m_parameters, scene
        );
    }

    if (m_parameters.blurAOTexture)
    {
        recordBlurAO(
            cmd,
            *m_ssaoPassResources.ambientOcclusion,
            m_gaussianBlurPassResources
        );
    }

    VkDescriptorSet const AOSet{
        m_parameters.blurAOTexture
            ? m_gaussianBlurPassResources.fullyBlurredImageSet
            : m_ssaoPassResources.ambientOcclusionSet
    };

    recordDrawShadowmap(
        cmd,
        m_shadowmappingPassResources,
        *m_lightingPassResources.shadowMap,
        m_parameters,
        scene
    );

    if (m_parameters.copyAOToOutputTexture)
    {
        texture.color().recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        m_ssaoPassResources.ambientOcclusion->recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );

        vkt::Image::recordCopyRect(
            cmd,
            m_ssaoPassResources.ambientOcclusion->image(),
            texture.color().image(),
            VK_IMAGE_ASPECT_COLOR_BIT,
            texture.size(),
            texture.size()
        );
    }
    else
    {
        recordDrawLighting(
            cmd,
            texture,
            frontFace,
            AOSet,
            m_lightingPassResources,
            m_parameters,
            scene
        );
    }

    // vkCmdBindShadersEXT(cmd, 1, &stage, nullptr);
}
void LightingPass::controlsWindow(std::optional<ImGuiID> dockNode)
{
    char const* const WINDOW_TITLE{"Controls"};

    vkt::UIWindowScope const sceneViewport{
        vkt::UIWindowScope::beginDockable(WINDOW_TITLE, dockNode)
    };

    if (!sceneViewport.isOpen())
    {
        return;
    }

    {
        ImGui::SeparatorText("Ambient Occlusion");
        PropertyTable table{PropertyTable::begin()};
        table.rowBoolean(
            {.name = "Enable AO from Front Face"},
            m_parameters.enableAOFromFrontFace,
            DEFAULT_PARAMETERS.enableAOFromFrontFace
        );
        table.rowBoolean(
            {.name = "Enable AO from Back Face"},
            m_parameters.enableAOFromBackFace,
            DEFAULT_PARAMETERS.enableAOFromBackFace
        );
        table.rowBoolean(
            {.name = "Reflect SSAO samples randomly"},
            m_parameters.enableRandomNormalSampling,
            DEFAULT_PARAMETERS.enableRandomNormalSampling
        );

        table.childPropertyBegin(false);
        ImGui::BeginDisabled(!m_parameters.enableRandomNormalSampling);
        table.rowBoolean(
            {.name = "Normalize Reflection Normals"},
            m_parameters.normalizeRandomNormals,
            DEFAULT_PARAMETERS.normalizeRandomNormals
        );
        ImGui::EndDisabled();
        table.childPropertyEnd();

        PropertySliderBehavior constexpr RADIUS_BEHAVIOR{
            .speed = 0.0001F,
        };
        table.rowFloat(
            {.name = "AO Occluder Radius"},
            m_parameters.occluderRadius,
            DEFAULT_PARAMETERS.occluderRadius,
            RADIUS_BEHAVIOR
        );

        PropertySliderBehavior constexpr BIAS_BEHAVIOR{
            .speed = 0.001F,
        };
        table.rowFloat(
            {.name = "AO Occluder Bias"},
            m_parameters.occluderBias,
            DEFAULT_PARAMETERS.occluderBias,
            BIAS_BEHAVIOR
        );

        PropertySliderBehavior constexpr SCALE_BEHAVIOR{
            .speed = 0.01F,
        };
        table.rowFloat(
            {.name = "AO Sample Scale",
             .tooltip = "Scales the length of AO occluder samples from the "
                        "occludee texel."},
            m_parameters.aoScale,
            DEFAULT_PARAMETERS.aoScale,
            SCALE_BEHAVIOR
        );
        table.rowBoolean(
            {.name = "Copy AO to Output Texture"},
            m_parameters.copyAOToOutputTexture,
            DEFAULT_PARAMETERS.copyAOToOutputTexture
        );
        table.rowBoolean(
            {.name = "Apply Gaussian Blur to AO"},
            m_parameters.blurAOTexture,
            DEFAULT_PARAMETERS.blurAOTexture
        );

        table.end();
    }

    {
        ImGui::SeparatorText("Scene Lighting");

        PropertyTable table{PropertyTable::begin()};
        PropertySliderBehavior constexpr AXIS_ANGLE_BEHAVIOR{
            .bounds =
                FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
        };

        table.rowVec3(
            {.name = "Light Axis Angles"},
            m_parameters.lightAxisAngles,
            DEFAULT_PARAMETERS.lightAxisAngles,
            AXIS_ANGLE_BEHAVIOR
        );

        PropertySliderBehavior constexpr LIGHT_STRENGTH_BEHAVIOR{
            .speed = 0.1F,
            .bounds = FloatBounds{.min = 0.0F},
        };
        table.rowFloat(
            {.name = "Directional Light Strength"},
            m_parameters.lightStrength,
            DEFAULT_PARAMETERS.lightStrength,
            LIGHT_STRENGTH_BEHAVIOR
        );
        table.rowFloat(
            {.name = "Ambient Strength"},
            m_parameters.ambientStrength,
            DEFAULT_PARAMETERS.ambientStrength,
            LIGHT_STRENGTH_BEHAVIOR
        );
        table.rowBoolean(
            {.name = "Override Albedo/Specular as White"},
            m_parameters.gbufferWhiteOverride,
            DEFAULT_PARAMETERS.gbufferWhiteOverride
        );

        table.rowBoolean(
            {.name = "Enable Shadows"},
            m_parameters.enableShadows,
            DEFAULT_PARAMETERS.enableShadows
        );

        PropertySliderBehavior constexpr DEPTH_BIAS_BEHAVIOR{
            .speed = 0.01F,
        };
        table.rowFloat(
            {.name = "Shadow Reciever Plane Depth Bias",
             .tooltip =
                 "All shadow receiving texels are offset by their surface "
                 "normal, which is linearly scaled by the cosine of the "
                 "normal's angle with the light. This factor provides final "
                 "manual scaling to that offset."},
            m_parameters.shadowReceiverPlaneDepthBias,
            DEFAULT_PARAMETERS.shadowReceiverPlaneDepthBias,
            DEPTH_BIAS_BEHAVIOR
        );

        table.rowFloat(
            {.name = "Shadow Receiver Constant Bias"},
            m_parameters.shadowReceiverConstantBias,
            DEFAULT_PARAMETERS.shadowReceiverConstantBias,
            DEPTH_BIAS_BEHAVIOR
        );

        table.rowFloat(
            {.name = "Fixed Function Depth Bias Constant"},
            m_parameters.depthBiasConstant,
            DEFAULT_PARAMETERS.depthBiasConstant,
            DEPTH_BIAS_BEHAVIOR
        );
        table.rowFloat(
            {.name = "Fixed Function Depth Bias Slope"},
            m_parameters.depthBiasSlope,
            DEFAULT_PARAMETERS.depthBiasSlope,
            DEPTH_BIAS_BEHAVIOR
        );

        PropertySliderBehavior const PROJECTION_EXTENT_BEHAVIOR{
            .speed = 0.01F,
            .bounds =
                FloatBounds{
                    .min = 0.0F,
                }
        };
        table.rowFloat(
            {.name = "Light Projection Width"},
            m_parameters.lightProjViewExtent.x,
            DEFAULT_PARAMETERS.lightProjViewExtent.x,
            PROJECTION_EXTENT_BEHAVIOR
        );
        table.rowFloat(
            {.name = "Light Projection Height"},
            m_parameters.lightProjViewExtent.y,
            DEFAULT_PARAMETERS.lightProjViewExtent.y,
            PROJECTION_EXTENT_BEHAVIOR
        );

        PropertySliderBehavior const DEPTH_NEAR_BEHAVIOR{
            .speed = 0.01F,
            .bounds =
                FloatBounds{.min = 0.0F, .max = m_parameters.shadowFarPlane}
        };
        table.rowFloat(
            {.name = "Light Shadow Depth Near Plane"},
            m_parameters.shadowNearPlane,
            glm::min(
                DEFAULT_PARAMETERS.shadowNearPlane, m_parameters.shadowFarPlane
            ),
            DEPTH_NEAR_BEHAVIOR
        );

        PropertySliderBehavior const DEPTH_FAR_BEHAVIOR{
            .speed = 0.01F,
            .bounds =
                FloatBounds{
                    .min = m_parameters.shadowNearPlane,
                }
        };
        table.rowFloat(
            {.name = "Light Shadow Depth Far Plane"},
            m_parameters.shadowFarPlane,
            glm::max(
                DEFAULT_PARAMETERS.shadowFarPlane, m_parameters.shadowNearPlane
            ),
            DEPTH_FAR_BEHAVIOR
        );

        table.end();
    }
}
} // namespace vkt