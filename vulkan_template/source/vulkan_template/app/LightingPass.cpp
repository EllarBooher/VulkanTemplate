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
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanOverloads.hpp"
#include <cassert>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
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
    glm::vec3 padding0;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(PushConstant) == 152ULL);
} // namespace

namespace vkt
{
// NOLINTBEGIN(readability-magic-numbers)
LightingPassParameters LightingPass::DEFAULT_PARAMETERS{
    .enableAO = true,

    .lightAxisAngles = glm::vec3{0.0F, 1.3F, 0.8F},
    .lightStrength = 10.0F,
    .ambientStrength = 0.1F,

    .occluderRadius = 0.04F,
    .occluderBias = 0.25F,
    .aoScale = 10.0F,
};
// NOLINTEND(readability-magic-numbers)

auto LightingPass::operator=(LightingPass&& other) -> LightingPass&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);

    m_renderTargetLayout =
        std::exchange(other.m_renderTargetLayout, VK_NULL_HANDLE);
    m_gbufferLayout = std::exchange(other.m_gbufferLayout, VK_NULL_HANDLE);

    m_shaderWithoutAO = std::exchange(other.m_shaderWithoutAO, VK_NULL_HANDLE);
    m_shaderWithAO = std::exchange(other.m_shaderWithAO, VK_NULL_HANDLE);
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

    vkDestroyShaderEXT(m_device, m_shaderWithoutAO, nullptr);
    vkDestroyShaderEXT(m_device, m_shaderWithAO, nullptr);
    vkDestroyPipelineLayout(m_device, m_shaderLayout, nullptr);
}
auto LightingPass::create(VkDevice const device) -> std::optional<LightingPass>
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

    std::vector<VkDescriptorSetLayout> const layouts{
        lightingPass.m_renderTargetLayout, lightingPass.m_gbufferLayout
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
        {.constantID = 0, .offset = 0, .size = 4ULL}
    };
    struct SpecializationHeader
    {
        uint32_t includeAO;
    };
    SpecializationHeader specializationData{};

    VkSpecializationInfo const specialization{
        VKR_ARRAY(specializationEntries),
        sizeof(SpecializationHeader),
        &specializationData
    };

    char const* SHADER_PATH{"shaders/deferred/light.comp.spv"};
    {
        specializationData.includeAO = 1;
        std::optional<VkShaderEXT> const shaderResult = loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            specialization
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        lightingPass.m_shaderWithAO = shaderResult.value();
    }
    {
        specializationData.includeAO = 0;
        std::optional<VkShaderEXT> const shaderResult = loadShaderObject(
            device,
            SHADER_PATH,
            VK_SHADER_STAGE_COMPUTE_BIT,
            (VkFlags)0,
            layouts,
            ranges,
            specialization
        );
        if (!shaderResult.has_value())
        {
            VKT_ERROR("Failed to compile shader.");
            return std::nullopt;
        }
        lightingPass.m_shaderWithoutAO = shaderResult.value();
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
        texture.singletonDescriptor(), gbuffer.descriptor()
    };

    texture.color().recordTransitionBarriered(cmd, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue const clearColor{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
    texture.color().image().recordClearEntireColor(cmd, &clearColor);

    gbuffer.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    vkCmdBindShadersEXT(
        cmd,
        1,
        &stage,
        m_parameters.enableAO ? &m_shaderWithAO : &m_shaderWithoutAO
    );

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

    PropertyTable table{PropertyTable::begin()};
    table.rowBoolean(
        "Enable AO", m_parameters.enableAO, DEFAULT_PARAMETERS.enableAO
    );

    PropertySliderBehavior constexpr AXIS_ANGLE_BEHAVIOR{
        .bounds = FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
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

    table.end();
}
} // namespace vkt