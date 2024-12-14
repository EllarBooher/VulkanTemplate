#include "Renderer.hpp"

#include "vulkan_template/app/Mesh.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/ImageOperations.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/Pipeline.hpp"
#include "vulkan_template/vulkan/Shader.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <filesystem>
#include <functional>
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
#include <vector>

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

auto cameraProjView(float const aspectRatio) -> glm::mat4x4
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
auto Renderer::operator=(Renderer&& other) -> Renderer&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_fragmentStage = std::exchange(other.m_fragmentStage, VK_NULL_HANDLE);
    m_vertexStage = std::exchange(other.m_vertexStage, VK_NULL_HANDLE);

    m_graphicsLayout = std::exchange(other.m_graphicsLayout, VK_NULL_HANDLE);
    m_graphicsPipeline =
        std::exchange(other.m_graphicsPipeline, VK_NULL_HANDLE);
    m_models = std::move(other.m_models);
    m_modelInverseTransposes = std::move(other.m_modelInverseTransposes);

    return *this;
}
Renderer::Renderer(Renderer&& other) noexcept { *this = std::move(other); }
Renderer::~Renderer()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_graphicsLayout, nullptr);
        vkDestroyShaderModule(m_device, m_vertexStage, nullptr);
        vkDestroyShaderModule(m_device, m_fragmentStage, nullptr);
    }
}
auto Renderer::create(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue& modelUploadQueue,
    RendererArguments const arguments
) -> std::optional<Renderer>
{
    std::optional<Renderer> result{std::in_place, Renderer{}};
    Renderer& renderer{result.value()};
    renderer.m_device = device;

    std::filesystem::path const vertexPath{"shaders/geometry.vert.spv"};
    std::filesystem::path const fragmentPath{"shaders/geometry.frag.spv"};

    std::vector<VkDescriptorSetLayout> const layouts{};
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = 96
    }};

    std::optional<VkShaderModule> const vertexShaderResult{
        vkt::loadShaderModule(device, vertexPath)
    };
    std::optional<VkShaderModule> const fragmentShaderResult{
        vkt::loadShaderModule(device, fragmentPath)
    };
    if (!vertexShaderResult.has_value() || !fragmentShaderResult.has_value())
    {
        VKT_ERROR("Failed to compile shader.");
        return std::nullopt;
    }

    renderer.m_fragmentStage = fragmentShaderResult.value();
    renderer.m_vertexStage = vertexShaderResult.value();

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
            device, &layoutCreateInfo, nullptr, &renderer.m_graphicsLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    PipelineBuilder pipelineBuilder{};
    pipelineBuilder.pushShader(
        renderer.m_vertexStage, VK_SHADER_STAGE_VERTEX_BIT, "main"
    );
    pipelineBuilder.pushShader(
        renderer.m_fragmentStage, VK_SHADER_STAGE_FRAGMENT_BIT, "main"
    );

    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);

    pipelineBuilder.setCullMode(
        VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE
    );

    pipelineBuilder.setMultisamplingNone();

    pipelineBuilder.enableDepthTest(
        true,
        arguments.reverseZ ? VK_COMPARE_OP_GREATER_OR_EQUAL
                           : VK_COMPARE_OP_LESS_OR_EQUAL
    );

    pipelineBuilder.setColorAttachment(arguments.color);
    pipelineBuilder.setDepthFormat(arguments.depth);

    renderer.m_graphicsPipeline =
        pipelineBuilder.buildPipeline(device, renderer.m_graphicsLayout);

    std::vector<glm::mat4x4> const models{glm::identity<glm::mat4x4>()};

    auto const bufferSize{static_cast<VkDeviceSize>(models.size())};

    renderer.m_models = std::make_unique<TStagedBuffer<glm::mat4x4>>(
        TStagedBuffer<glm::mat4x4>::allocate(
            device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, bufferSize
        )
    );
    renderer.m_modelInverseTransposes =
        std::make_unique<TStagedBuffer<glm::mat4x4>>(
            TStagedBuffer<glm::mat4x4>::allocate(
                device,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                allocator,
                bufferSize
            )
        );

    std::span<glm::mat4x4> const modelsMapped{
        renderer.m_models->mapFullCapacity()
    };
    std::span<glm::mat4x4> const modelInverseTransposesMapped{
        renderer.m_modelInverseTransposes->mapFullCapacity()
    };
    renderer.m_models->resizeStaged(bufferSize);
    renderer.m_modelInverseTransposes->resizeStaged(bufferSize);

    for (size_t index{0}; index < models.size(); index++)
    {
        modelsMapped[index] = models[index];
        modelInverseTransposesMapped[index] =
            glm::inverseTranspose(models[index]);
    }

    modelUploadQueue.immediateSubmit(
        [&](VkCommandBuffer cmd)
    {
        renderer.m_models->recordCopyToDevice(cmd);
        renderer.m_modelInverseTransposes->recordCopyToDevice(cmd);
    }
    );

    return result;
}
void Renderer::recordDraw(
    VkCommandBuffer const cmd, RenderTarget& renderTarget, Mesh const& mesh
)
{
    renderTarget.depth().recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
    );
    renderTarget.color().recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    );

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

        .clearValue = VkClearValue{.depthStencil{.depth = 0.0F}},
    };

    VkRenderingAttachmentInfo const colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,

        .imageView = renderTarget.color().view(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,

        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

        .clearValue =
            VkClearValue{
                .color{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}},
            },
    };

    std::vector<VkRenderingAttachmentInfo> const colorAttachments{
        colorAttachment
    };
    VkRenderingInfo const renderInfo{
        renderingInfo(renderTarget.size(), colorAttachments, &depthAttachment)
    };

    vkCmdBeginRendering(cmd, &renderInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    VkViewport const viewport{
        .x = static_cast<float>(renderTarget.size().offset.x),
        .y = static_cast<float>(renderTarget.size().offset.y),
        .width = static_cast<float>(renderTarget.size().extent.width),
        .height = static_cast<float>(renderTarget.size().extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D const scissor{renderTarget.size()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    {
        auto const aspectRatio{static_cast<float>(
            vkt::aspectRatio(renderTarget.size().extent).value()
        )};
        glm::mat4x4 const cameraProjView{detail::cameraProjView(aspectRatio)};

        MeshBuffers& meshBuffers{*mesh.meshBuffers};

        detail::PushConstantVertex const vertexPushConstant{
            .vertexBuffer = meshBuffers.vertexAddress(),
            .modelBuffer = m_models->deviceAddress(),
            .modelInverseTransposeBuffer =
                m_modelInverseTransposes->deviceAddress(),
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

        for (GeometrySurface const& surface : mesh.surfaces)
        {
            vkCmdDrawIndexed(
                cmd,
                surface.indexCount,
                m_models->deviceSize(),
                surface.firstIndex,
                0,
                0
            );
        }
    }

    vkCmdEndRendering(cmd);
}
} // namespace vkt