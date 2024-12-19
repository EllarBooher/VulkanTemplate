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
#include <cassert>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <utility>
#include <vector>

namespace
{
struct PushConstant
{
    glm::vec2 offset;
    glm::vec2 gBufferCapacity;

    glm::vec4 cameraPosition;

    glm::vec4 lightForward;

    glm::mat4x4 cameraProjView;

    glm::vec2 extent;

    float occluderRadius;
    float occluderBias;
    float aoScale;
    float lightStrength;

    float ambientStrength;
    uint32_t gbufferWhiteOverride;
    glm::vec2 padding0;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(PushConstant) == 152ULL);

struct SpecializationConstant
{
    uint32_t enableAO{VK_FALSE};
    uint32_t enableRandomNormalSampling{VK_FALSE};
    uint32_t normalizeRandomNormals{VK_FALSE};
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(SpecializationConstant) == 12ULL);

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

auto fillRandomNormalsSet(
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

} // namespace

template <> struct std::hash<::SpecializationConstant>
{
    auto operator()(::SpecializationConstant const& sc) const noexcept -> size_t
    {
        // A bit silly since these represent bools, but more general is better
        size_t h1{std::hash<uint32_t>{}(sc.enableAO)};
        size_t h2{std::hash<uint32_t>{}(sc.enableRandomNormalSampling)};
        size_t h3{std::hash<uint32_t>{}(sc.normalizeRandomNormals)};
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

namespace vkt
{
// NOLINTBEGIN(readability-magic-numbers)
LightingPassParameters LightingPass::DEFAULT_PARAMETERS{
    .enableAO = true,
    .enableRandomNormalSampling = true,
    .normalizeRandomNormals = true,

    .lightAxisAngles = glm::vec3{0.0F, 1.3F, 0.8F},
    .lightStrength = 0.0F,
    .ambientStrength = 1.0F,

    .occluderRadius = 1.0F,
    .occluderBias = 0.15F,
    .aoScale = 0.02F,
    .gbufferWhiteOverride = true,
};
// NOLINTEND(readability-magic-numbers)

auto LightingPass::operator=(LightingPass&& other) -> LightingPass&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);

    m_renderTargetLayout =
        std::exchange(other.m_renderTargetLayout, VK_NULL_HANDLE);
    m_gbufferLayout = std::exchange(other.m_gbufferLayout, VK_NULL_HANDLE);
    m_randomNormalsLayout =
        std::exchange(other.m_randomNormalsLayout, VK_NULL_HANDLE);

    m_randomNormalsSet =
        std::exchange(other.m_randomNormalsSet, VK_NULL_HANDLE);
    m_randomNormals = std::exchange(other.m_randomNormals, nullptr);
    m_descriptorAllocator = std::exchange(other.m_descriptorAllocator, nullptr);

    m_shadersBySpecializationHash =
        std::exchange(other.m_shadersBySpecializationHash, {});
    m_shaderLayout = std::exchange(other.m_shaderLayout, VK_NULL_HANDLE);

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

    vkDestroyDescriptorSetLayout(m_device, m_renderTargetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_gbufferLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_randomNormalsLayout, nullptr);

    for (auto const& [hash, shader] : m_shadersBySpecializationHash)
    {
        vkDestroyShaderEXT(m_device, shader, nullptr);
    }

    vkDestroyPipelineLayout(m_device, m_shaderLayout, nullptr);
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

    auto const renderTargetLayoutResult{
        vkt::RenderTarget::allocateSingletonLayout(device)
    };
    if (!renderTargetLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate render target descriptor set layout.");
        return std::nullopt;
    }
    lightingPass.m_renderTargetLayout = renderTargetLayoutResult.value();

    auto const gbufferLayoutResult{
        vkt::GBuffer::allocateDescriptorSetLayout(device)
    };
    if (!gbufferLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    lightingPass.m_gbufferLayout = gbufferLayoutResult.value();

    auto const randomNormalsLayoutResult{::allocateRandomNormalsLayout(device)};
    if (!randomNormalsLayoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate GBuffer descriptor set layout.");
        return std::nullopt;
    }
    lightingPass.m_randomNormalsLayout = randomNormalsLayoutResult.value();

    std::vector<VkDescriptorSetLayout> const layouts{
        lightingPass.m_renderTargetLayout,
        lightingPass.m_gbufferLayout,
        lightingPass.m_randomNormalsLayout
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(::PushConstant),
    }};

    // light.comp snippet
    //
    // layout(constant_id = 0) const bool includeAO = false;
    //
    std::vector<VkSpecializationMapEntry> const specializationEntries{
        {.constantID = 0,
         .offset = offsetof(SpecializationConstant, enableAO),
         .size = sizeof(SpecializationConstant::enableAO)},
        {.constantID = 1,
         .offset = offsetof(SpecializationConstant, enableRandomNormalSampling),
         .size = sizeof(SpecializationConstant::enableRandomNormalSampling)},
        {.constantID = 2,
         .offset = offsetof(SpecializationConstant, normalizeRandomNormals),
         .size = sizeof(SpecializationConstant::normalizeRandomNormals)}
    };

    // Some combinations of features don't make sense, such as disabling AO and
    // enabling any other features, but there is no real need to worry about
    // that.
    std::vector<SpecializationConstant> const specializationConstants{
        {VK_FALSE, VK_FALSE, VK_FALSE},
        {VK_FALSE, VK_FALSE, VK_TRUE},
        {VK_FALSE, VK_TRUE, VK_FALSE},
        {VK_FALSE, VK_TRUE, VK_TRUE},
        {VK_TRUE, VK_FALSE, VK_FALSE},
        {VK_TRUE, VK_FALSE, VK_TRUE},
        {VK_TRUE, VK_TRUE, VK_FALSE},
        {VK_TRUE, VK_TRUE, VK_TRUE}
    };
    char const* SHADER_PATH{"shaders/deferred/light.comp.spv"};
    for (auto const& specialization : specializationConstants)
    {
        VkSpecializationInfo const specializationInfo{
            VKR_ARRAY(specializationEntries),
            sizeof(SpecializationConstant),
            &specialization
        };
        std::optional<VkShaderEXT> const shaderResult = loadShaderObject(
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
        size_t const hash{std::hash<SpecializationConstant>{}(specialization)};
        lightingPass.m_shadersBySpecializationHash[hash] = shaderResult.value();
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
            device, &layoutCreateInfo, nullptr, &lightingPass.m_shaderLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    auto randomNormalsResult{
        ::createRandomNormalsImage(device, allocator, submissionQueue)
    };
    if (!randomNormalsResult.has_value())
    {
        VKT_ERROR(
            "Failed to create random normals image for Lighting Pass pipeline."
        );
        return std::nullopt;
    }
    lightingPass.m_randomNormals =
        std::make_unique<ImageView>(std::move(randomNormalsResult).value());

    size_t constexpr MAX_SETS{1};
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

    auto randomNormalsSetResult{::fillRandomNormalsSet(
        device,
        *lightingPass.m_randomNormals,
        *lightingPass.m_descriptorAllocator,
        lightingPass.m_randomNormalsLayout
    )};
    if (!randomNormalsSetResult.has_value())
    {
        VKT_ERROR("Failed to fill random normals descriptor set for Lighting "
                  "Pass pipeline.");
        return std::nullopt;
    }
    lightingPass.m_randomNormalsSet = randomNormalsSetResult.value();

    return lightingPassResult;
}
void LightingPass::recordDraw(
    VkCommandBuffer const cmd,
    RenderTarget& texture,
    GBuffer const& gbuffer,
    Scene const& scene
)
{
    assert(
        texture.size() == gbuffer.size()
        && "GBuffer and render target must be same size."
    );

    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    std::vector<VkDescriptorSet> descriptors{
        texture.singletonDescriptor(), gbuffer.descriptor(), m_randomNormalsSet
    };

    texture.color().recordTransitionBarriered(cmd, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue const clearColor{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
    texture.color().image().recordClearEntireColor(cmd, &clearColor);

    gbuffer.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    SpecializationConstant const specializationConstant{
        .enableAO = m_parameters.enableAO,
        .enableRandomNormalSampling = m_parameters.enableRandomNormalSampling,
        .normalizeRandomNormals = m_parameters.normalizeRandomNormals,
    };

    VkShaderEXT const shader{m_shadersBySpecializationHash.at(
        std::hash<SpecializationConstant>{}(specializationConstant)
    )};
    vkCmdBindShadersEXT(cmd, 1, &stage, &shader);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_shaderLayout,
        0,
        VKR_ARRAY(descriptors),
        VKR_ARRAY_NONE
    );

    uint32_t constexpr WORKGROUP_SIZE{16};

    VkExtent2D const gBufferCapacity{gbuffer.capacity().value()};

    auto const aspectRatio{
        static_cast<float>(vkt::aspectRatio(texture.size().extent).value())
    };

    auto const X{glm::angleAxis(
        m_parameters.lightAxisAngles.x, glm::vec3{1.0F, 0.0F, 0.0F}
    )};
    auto const Y{glm::angleAxis(
        m_parameters.lightAxisAngles.y, glm::vec3{0.0F, 1.0F, 0.0F}
    )};
    auto const Z{glm::angleAxis(
        m_parameters.lightAxisAngles.z, glm::vec3{0.0F, 0.0F, 1.0F}
    )};
    glm::vec4 const lightForward{
        glm::toMat4(Z * X * Y) * glm::vec4(0.0, 0.0, 1.0, 0.0)
    };

    VkRect2D const drawRect{texture.size()};
    ::PushConstant const pc{
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
        .cameraPosition = glm::vec4{scene.camera().position, 1.0F},
        .lightForward = lightForward,
        .cameraProjView = scene.cameraProjView(aspectRatio),
        .extent =
            glm::vec2{
                static_cast<float>(drawRect.extent.width),
                static_cast<float>(drawRect.extent.height)
            },
        .occluderRadius = m_parameters.occluderRadius,
        .occluderBias = m_parameters.occluderBias,
        .aoScale = m_parameters.aoScale,
        .lightStrength = m_parameters.lightStrength,
        .ambientStrength = m_parameters.ambientStrength,
        .gbufferWhiteOverride = m_parameters.gbufferWhiteOverride,
    };

    vkCmdPushConstants(
        cmd,
        m_shaderLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(::PushConstant),
        &pc
    );

    vkt::computeDispatch(
        cmd,
        VkExtent3D{drawRect.extent.width, drawRect.extent.height, 1},
        WORKGROUP_SIZE
    );

    vkCmdBindShadersEXT(cmd, 1, &stage, nullptr);
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
            "Enable AO", m_parameters.enableAO, DEFAULT_PARAMETERS.enableAO
        );
        table.rowBoolean(
            "Reflect SSAO samples randomly",
            m_parameters.enableRandomNormalSampling,
            DEFAULT_PARAMETERS.enableRandomNormalSampling
        );

        table.childPropertyBegin(false);
        ImGui::BeginDisabled(!m_parameters.enableRandomNormalSampling);
        table.rowBoolean(
            "Normalize Reflection Normals",
            m_parameters.normalizeRandomNormals,
            DEFAULT_PARAMETERS.normalizeRandomNormals
        );
        ImGui::EndDisabled();
        table.childPropertyEnd();

        PropertySliderBehavior constexpr RADIUS_BEHAVIOR{
            .speed = 0.0001F,
        };
        table.rowFloat(
            "AO Occluder Radius",
            m_parameters.occluderRadius,
            DEFAULT_PARAMETERS.occluderRadius,
            RADIUS_BEHAVIOR
        );

        PropertySliderBehavior constexpr BIAS_BEHAVIOR{
            .speed = 0.001F,
        };
        table.rowFloat(
            "AO Occluder Bias",
            m_parameters.occluderBias,
            DEFAULT_PARAMETERS.occluderBias,
            BIAS_BEHAVIOR
        );

        PropertySliderBehavior constexpr SCALE_BEHAVIOR{
            .speed = 0.01F,
        };
        table.rowFloat(
            "AO Sample Scale",
            m_parameters.aoScale,
            DEFAULT_PARAMETERS.aoScale,
            SCALE_BEHAVIOR
        );
        table.rowBoolean(
            "Override GBuffer color as white",
            m_parameters.gbufferWhiteOverride,
            DEFAULT_PARAMETERS.gbufferWhiteOverride
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
            "Light Axis Angles",
            m_parameters.lightAxisAngles,
            DEFAULT_PARAMETERS.lightAxisAngles,
            AXIS_ANGLE_BEHAVIOR
        );

        PropertySliderBehavior constexpr LIGHT_STRENGTH_BEHAVIOR{
            .speed = 0.1F,
            .bounds = FloatBounds{.min = 0.0F},
        };
        table.rowFloat(
            "Directional Light Strength",
            m_parameters.lightStrength,
            DEFAULT_PARAMETERS.lightStrength,
            LIGHT_STRENGTH_BEHAVIOR
        );
        table.rowFloat(
            "Ambient Strength",
            m_parameters.ambientStrength,
            DEFAULT_PARAMETERS.ambientStrength,
            LIGHT_STRENGTH_BEHAVIOR
        );

        table.end();
    }
}
} // namespace vkt