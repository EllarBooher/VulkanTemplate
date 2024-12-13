#include "Pipeline.hpp"

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <utility>

namespace vkt
{
auto PipelineBuilder::buildPipeline(
    VkDevice const device, VkPipelineLayout const layout
) const -> VkPipeline
{
    VkPipelineViewportStateCreateInfo const viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,

        .viewportCount = 1,
        .scissorCount = 1,

        // We use dynamic rendering, so no other members are needed
    };

    std::vector<VkFormat> colorFormats{};
    std::vector<VkPipelineColorBlendAttachmentState> attachmentStates{};
    if (m_colorAttachment.has_value())
    {
        ColorAttachmentSpecification const specification{
            m_colorAttachment.value()
        };

        colorFormats.push_back(specification.format);
        attachmentStates.push_back(specification.blending);
    }

    VkPipelineColorBlendStateCreateInfo const colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,

        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,

        .attachmentCount = static_cast<uint32_t>(attachmentStates.size()),
        .pAttachments = attachmentStates.data(),
    };

    VkPipelineRenderingCreateInfo const renderInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,

        .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.data(),

        .depthAttachmentFormat = m_depthAttachmentFormat,
    };

    // Dummy vertex input
    VkPipelineVertexInputStateCreateInfo const vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    std::vector<VkDynamicState> dynamicStates{
        m_dynamicStates.begin(), m_dynamicStates.end()
    };

    // We insert these by default since we have no methods for setting
    // the static state for now
    dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);

    VkPipelineDynamicStateCreateInfo const dynamicInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};
    shaderStages.reserve(m_shaderStages.size());
    for (auto const& stage : m_shaderStages)
    {
        shaderStages.push_back(pipelineShaderStageCreateInfo(
            stage.stage, stage.shader, stage.entryPoint.c_str()
        ));
    }

    VkGraphicsPipelineCreateInfo const pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,

        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),

        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &m_inputAssembly,

        .pTessellationState = nullptr,

        .pViewportState = &viewportState,
        .pRasterizationState = &m_rasterizer,
        .pMultisampleState = &m_multisampling,

        .pDepthStencilState = &m_depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicInfo,

        .layout = layout,
        .renderPass = VK_NULL_HANDLE, // dynamic rendering used
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };

    VkPipeline pipeline{VK_NULL_HANDLE};
    VKT_LOG_VK(
        vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline
        ),
        "Building graphics pipeline"
    );
    return pipeline;
}

void PipelineBuilder::pushShader(
    VkShaderModule const shader,
    VkShaderStageFlagBits const stage,
    std::string const& entryPoint
)
{
    // Copy string to keep it alive until consumption
    m_shaderStages.push_back(ShaderStageSpecification{
        .stage = stage, .shader = shader, .entryPoint = entryPoint
    });
}

void PipelineBuilder::setInputTopology(VkPrimitiveTopology const topology)
{
    m_inputAssembly.topology = topology;
    m_inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::setPolygonMode(VkPolygonMode const mode)
{
    m_rasterizer.polygonMode = mode;
}

void PipelineBuilder::pushDynamicState(VkDynamicState const dynamicState)
{
    m_dynamicStates.insert(dynamicState);
}

void PipelineBuilder::setCullMode(
    VkCullModeFlags const cullMode, VkFrontFace const frontFace
)
{
    m_rasterizer.cullMode = cullMode;
    m_rasterizer.frontFace = frontFace;
}

void PipelineBuilder::setMultisamplingNone()
{
    m_multisampling.sampleShadingEnable = VK_FALSE;
    m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_multisampling.minSampleShading = 1.0F;
    m_multisampling.pSampleMask = nullptr;
    m_multisampling.alphaToCoverageEnable = VK_FALSE;
    m_multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineBuilder::setColorAttachment(VkFormat const format)
{
    m_colorAttachment = ColorAttachmentSpecification{
        .format = format,
    };
}

void PipelineBuilder::setDepthFormat(VkFormat const format)
{
    m_depthAttachmentFormat = format;
}

void PipelineBuilder::enableDepthBias()
{
    m_rasterizer.depthBiasEnable = VK_TRUE;
}

void PipelineBuilder::disableDepthTest()
{
    m_depthStencil = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_NEVER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F,
    };
}

void PipelineBuilder::enableDepthTest(
    bool const depthWriteEnable, VkCompareOp const compareOp
)
{
    m_depthStencil = VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE,
        .depthCompareOp = compareOp,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F,
    };
}

} // namespace vkt