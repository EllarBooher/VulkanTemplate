#pragma once

#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vkt
{
struct PipelineBuilder
{
public:
    PipelineBuilder() = default;

    auto buildPipeline(VkDevice device, VkPipelineLayout layout) const
        -> VkPipeline;

    void pushShader(
        VkShaderModule shader,
        VkShaderStageFlagBits stage,
        std::string const& entryPoint
    );

    void setInputTopology(VkPrimitiveTopology topology);

    void setPolygonMode(VkPolygonMode mode);

    void pushDynamicState(VkDynamicState dynamicState);

    void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);

    void setMultisamplingNone();

    void setColorAttachment(VkFormat format);

    void setDepthFormat(VkFormat format);

    void enableDepthBias();

    void disableDepthTest();

    void enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp);

private:
    struct ShaderStageSpecification
    {
        VkShaderStageFlagBits stage;
        VkShaderModule shader;
        std::string entryPoint;
    };
    std::vector<ShaderStageSpecification> m_shaderStages{};
    std::set<VkDynamicState> m_dynamicStates{};

    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    };
    VkPipelineRasterizationStateCreateInfo m_rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0,
    };

    struct ColorAttachmentSpecification
    {
        VkFormat format{VK_FORMAT_UNDEFINED};

        // TODO: expose blending in pipeline builder
        VkPipelineColorBlendAttachmentState blending{
            .blendEnable = VK_FALSE,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
    };

    VkPipelineMultisampleStateCreateInfo m_multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    };
    VkPipelineDepthStencilStateCreateInfo m_depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };

    std::optional<ColorAttachmentSpecification> m_colorAttachment{};
    VkFormat m_depthAttachmentFormat{VK_FORMAT_UNDEFINED};
};
} // namespace vkt