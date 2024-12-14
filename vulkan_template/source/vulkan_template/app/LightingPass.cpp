#include "LightingPass.hpp"

#include "vulkan_template/app/GBuffer.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanOverloads.hpp"
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <utility>

namespace detail
{
struct PushConstant
{
    glm::vec2 offset;
    glm::vec2 gBufferCapacity;

    glm::vec4 cameraPosition;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(PushConstant) == 32ULL);
} // namespace detail

namespace vkt
{
auto LightingPass::operator=(LightingPass&& other) -> LightingPass&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);

    m_renderTargetLayout =
        std::exchange(other.m_renderTargetLayout, VK_NULL_HANDLE);
    m_gbufferLayout = std::exchange(other.m_gbufferLayout, VK_NULL_HANDLE);

    m_shader = std::exchange(other.m_shader, VK_NULL_HANDLE);
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

    vkDestroyShaderEXT(m_device, m_shader, nullptr);
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
        .size = sizeof(detail::PushConstant),
    }};
    VkSpecializationInfo const specialization{};

    char const* SHADER_PATH{"shaders/deferred/light.comp.spv"};
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
    lightingPass.m_shader = shaderResult.value();

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
    VkCommandBuffer const cmd, RenderTarget& texture, GBuffer const& gbuffer
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

    VkClearColorValue const clearColor{.float32 = {0.2F, 0.0F, 0.0F, 1.0F}};
    texture.color().image().recordClearEntireColor(cmd, &clearColor);

    gbuffer.recordTransitionImages(
        cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    vkCmdBindShadersEXT(cmd, 1, &stage, &m_shader);

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

    VkRect2D const drawRect{texture.size()};
    detail::PushConstant const pc{
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
        .cameraPosition = glm::vec4{0.0F, 0.0F, -5.0F, 1.0F},
    };

    vkCmdPushConstants(
        cmd,
        m_shaderLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(detail::PushConstant),
        &pc
    );

    vkt::computeDispatch(
        cmd,
        VkExtent3D{drawRect.extent.width, drawRect.extent.height, 1},
        WORKGROUP_SIZE
    );

    vkCmdBindShadersEXT(cmd, 1, &stage, nullptr);
}
} // namespace vkt