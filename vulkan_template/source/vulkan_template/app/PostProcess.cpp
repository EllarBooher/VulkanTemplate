#include "PostProcess.hpp"

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
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

namespace detail
{
struct PushConstant
{
    glm::vec2 offset;
};
} // namespace detail

auto vkt::PostProcess::operator=(PostProcess&& other) -> PostProcess&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_oetfSRGB = std::exchange(other.m_oetfSRGB, VK_NULL_HANDLE);
    m_oetfSRGBLayout = std::exchange(other.m_oetfSRGBLayout, VK_NULL_HANDLE);
    m_transferSingletonLayout =
        std::exchange(other.m_transferSingletonLayout, VK_NULL_HANDLE);

    return *this;
}

vkt::PostProcess::PostProcess(PostProcess&& other) noexcept
{
    *this = std::move(other);
}

vkt::PostProcess::~PostProcess()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyShaderEXT(m_device, m_oetfSRGB, nullptr);
        vkDestroyPipelineLayout(m_device, m_oetfSRGBLayout, nullptr);
        vkDestroyDescriptorSetLayout(
            m_device, m_transferSingletonLayout, nullptr
        );
    }
}

auto vkt::PostProcess::create(VkDevice const device)
    -> std::optional<PostProcess>
{
    char const* OETF_SHADER_PATH{"shaders/oetf_srgb.comp.spv"};

    std::optional<PostProcess> result{std::in_place, PostProcess{}};
    PostProcess& postProcess{result.value()};

    postProcess.m_device = device;

    auto const layoutResult{
        DescriptorLayoutBuilder{}
            .addBinding(
                DescriptorLayoutBuilder::AddBindingParameters{
                    .binding = 0,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stageMask = VK_SHADER_STAGE_COMPUTE_BIT,
                    .bindingFlags = 0,
                },
                1
            )
            .build(device, 0)
    };

    if (!layoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate singleton descriptor layout.");
        return std::nullopt;
    }
    postProcess.m_transferSingletonLayout = layoutResult.value();

    std::vector<VkDescriptorSetLayout> const layouts{
        postProcess.m_transferSingletonLayout
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(detail::PushConstant)
    }};
    VkSpecializationInfo const specialization{};

    std::optional<VkShaderEXT> const shaderResult = loadShaderObject(
        device,
        OETF_SHADER_PATH,
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
    postProcess.m_oetfSRGB = shaderResult.value();

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
            device, &layoutCreateInfo, nullptr, &postProcess.m_oetfSRGBLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    return result;
}

void vkt::PostProcess::recordLinearToSRGB(
    VkCommandBuffer const cmd, RenderTarget& texture
)
{
    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    std::vector<VkDescriptorSet> descriptors{texture.singletonDescriptor()};

    texture.color().recordTransitionBarriered(cmd, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindShadersEXT(cmd, 1, &stage, &m_oetfSRGB);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_oetfSRGBLayout,
        0,
        VKR_ARRAY(descriptors),
        VKR_ARRAY_NONE
    );

    uint32_t constexpr WORKGROUP_SIZE{16};

    VkRect2D const drawRect{texture.size()};
    detail::PushConstant const pc{
        .offset =
            glm::vec2{
                static_cast<float>(drawRect.offset.x),
                static_cast<float>(drawRect.offset.y)
            }
    };

    vkCmdPushConstants(
        cmd,
        m_oetfSRGBLayout,
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
