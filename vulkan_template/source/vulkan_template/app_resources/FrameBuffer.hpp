#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>
#include <vector>

namespace vkt
{
struct Swapchain;
struct SceneTexture;
} // namespace vkt

namespace vkt
{
struct Frame
{
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer mainCommandBuffer{VK_NULL_HANDLE};

    // The semaphore that the swapchain signals when its
    // image is ready to be written to.
    VkSemaphore swapchainSemaphore{VK_NULL_HANDLE};

    // The semaphore that the swapchain waits on before presenting.
    VkSemaphore renderSemaphore{VK_NULL_HANDLE};

    // The fence that the CPU waits on to ensure the frame is not in use.
    VkFence renderFence{VK_NULL_HANDLE};

    void destroy(VkDevice);
};

struct FrameBuffer
{
public:
    auto operator=(FrameBuffer&&) -> FrameBuffer& = delete;
    FrameBuffer(FrameBuffer const&) = delete;
    auto operator=(FrameBuffer const&) -> FrameBuffer& = delete;

    FrameBuffer(FrameBuffer&&) noexcept;
    ~FrameBuffer();

private:
    FrameBuffer() = default;
    void destroy();

public:
    // QueueFamilyIndex should be capable of graphics/compute/transfer/present.
    static auto create(VkDevice, uint32_t queueFamilyIndex)
        -> std::optional<FrameBuffer>;

    [[nodiscard]] auto frameNumber() const -> size_t;

    // Prepares the frame for command recording. A return value of VK_RESULT
    // means that you may proceed to call currentFrame and record commands into
    // its command buffer.
    auto beginNewFrame() -> VkResult;

    [[nodiscard]] auto currentFrame() const -> Frame const&;

    // Ends the frame and presents it to the given swapchain
    [[nodiscard]] auto finishFrameWithPresent(
        Swapchain& swapchain,
        VkQueue submissionQueue,
        SceneTexture& sourceTexture
    ) -> VkResult;

private:
    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<Frame> m_frames{};
    size_t m_frameNumber{0};
};
} // namespace vkt