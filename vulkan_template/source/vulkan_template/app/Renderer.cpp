#include "Renderer.hpp"

#include "vulkan_template/app/SceneTexture.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include <filesystem>
#include <glm/vec2.hpp>
#include <span>
#include <utility>
#include <vector>

namespace vkt
{
Renderer::Renderer(Renderer&& other) noexcept
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_shader = std::exchange(other.m_shader, VK_NULL_HANDLE);
    m_shaderLayout = std::exchange(other.m_shaderLayout, VK_NULL_HANDLE);
    m_destinationSingletonLayout =
        std::exchange(other.m_destinationSingletonLayout, VK_NULL_HANDLE);
}
Renderer::~Renderer()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyShaderEXT(m_device, m_shader, nullptr);
        vkDestroyPipelineLayout(m_device, m_shaderLayout, nullptr);
        vkDestroyDescriptorSetLayout(
            m_device, m_destinationSingletonLayout, nullptr
        );
    }
}
auto Renderer::create(VkDevice const device) -> std::optional<Renderer>
{
    std::optional<Renderer> result{std::in_place, Renderer{}};
    Renderer& renderer{result.value()};
    renderer.m_device = device;

    std::filesystem::path const shaderPath{"shaders/testpattern.comp.spv"};

    if (auto const descriptorResult{SceneTexture::allocateSingletonLayout(device
        )};
        descriptorResult.has_value())
    {
        renderer.m_destinationSingletonLayout = descriptorResult.value();
    }
    else
    {
        VKT_ERROR("Failed to allocator descriptor set layout for scene texture."
        );
        return std::nullopt;
    }

    std::vector<VkDescriptorSetLayout> const layouts{
        renderer.m_destinationSingletonLayout
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 16
    }};

    std::optional<VkShaderEXT> const shaderResult{vkt::loadShaderObject(
        device,
        shaderPath,
        VK_SHADER_STAGE_COMPUTE_BIT,
        (VkFlags)0,
        layouts,
        ranges,
        VkSpecializationInfo{}
    )};
    if (!shaderResult.has_value())
    {
        VKT_ERROR("Failed to compile shaader.");
        return std::nullopt;
    }

    renderer.m_shader = shaderResult.value();

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
            device, &layoutCreateInfo, nullptr, &renderer.m_shaderLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    return result;
}
void Renderer::recordDraw(VkCommandBuffer const cmd, SceneTexture& destination)
    const
{
    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    VkPipelineBindPoint const bindPoint{VK_PIPELINE_BIND_POINT_COMPUTE};
    VkShaderEXT const shaderObject{m_shader};
    VkPipelineLayout const layout{m_shaderLayout};

    destination.color().recordTransitionBarriered(cmd, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindShadersEXT(cmd, 1, &stage, &shaderObject);

    // Bind the destination image for rendering during compute
    VkDescriptorSet destinationDescriptor{destination.singletonDescriptor()};
    vkCmdBindDescriptorSets(
        cmd, bindPoint, layout, 0, 1, &destinationDescriptor, VKR_ARRAY_NONE
    );

    struct PushConstant
    {
        glm::vec2 drawOffset{};
        glm::vec2 drawExtent{};
    };

    VkRect2D const drawRect{destination.size()};
    PushConstant const pc{
        .drawOffset =
            glm::vec2{
                static_cast<float>(drawRect.offset.x),
                static_cast<float>(drawRect.offset.y)
            },
        .drawExtent =
            glm::vec2{
                static_cast<float>(drawRect.extent.width),
                static_cast<float>(drawRect.extent.height)
            },
    };

    uint32_t constexpr BYTE_OFFSET{0};
    vkCmdPushConstants(
        cmd, layout, stage, BYTE_OFFSET, sizeof(PushConstant), &pc
    );

    uint32_t constexpr WORKGROUP_SIZE{16};

    vkt::computeDispatch(
        cmd,
        VkExtent3D{drawRect.extent.width, drawRect.extent.height, 1},
        WORKGROUP_SIZE
    );
}
} // namespace vkt