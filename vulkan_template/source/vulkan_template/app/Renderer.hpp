#pragma once

#include "vulkan_template/vulkan/Buffers.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>

namespace vkt
{
struct RenderTarget;
struct ImmediateSubmissionQueue;
struct Mesh;
} // namespace vkt

namespace vkt
{
struct Renderer
{
public:
    auto operator=(Renderer const&) -> Renderer& = delete;
    Renderer(Renderer const&) = delete;

    auto operator=(Renderer&&) -> Renderer&;
    Renderer(Renderer&&) noexcept;
    ~Renderer();

private:
    Renderer() = default;

public:
    struct RendererArguments
    {
        VkFormat color;
        VkFormat depth;
        bool reverseZ;
    };

    static auto create(
        VkDevice,
        VmaAllocator,
        ImmediateSubmissionQueue& modelUploadQueue,
        RendererArguments
    ) -> std::optional<Renderer>;

    void recordDraw(VkCommandBuffer, RenderTarget&, Mesh const&);

private:
    VkDevice m_device;

    VkShaderModule m_vertexStage{VK_NULL_HANDLE};
    VkShaderModule m_fragmentStage{VK_NULL_HANDLE};

    VkPipelineLayout m_graphicsLayout{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};

    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_models{};
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_modelInverseTransposes{};
};
} // namespace vkt