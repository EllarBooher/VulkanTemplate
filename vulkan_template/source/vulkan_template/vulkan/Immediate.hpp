#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <functional>
#include <optional>

namespace vkt
{
struct ImmediateSubmissionQueue
{
public:
    auto operator=(ImmediateSubmissionQueue const&)
        -> ImmediateSubmissionQueue = delete;
    ImmediateSubmissionQueue(ImmediateSubmissionQueue const&) = delete;

    auto operator=(ImmediateSubmissionQueue&& other) noexcept
        -> ImmediateSubmissionQueue&;
    ImmediateSubmissionQueue(ImmediateSubmissionQueue&& other) noexcept;

    ~ImmediateSubmissionQueue();

    // QueueFamilyIndex should match the queue, and the queue should be capable
    // of all commands required
    static auto create(VkDevice, VkQueue, uint32_t queueFamilyIndex)
        -> std::optional<ImmediateSubmissionQueue>;

    enum class SubmissionResult
    {
        FAILED,
        TIMEOUT,
        SUCCESS
    };

    // Provides a command buffer in the recording state, then submits it and
    // waits for all recorded commands to complete.
    auto
    immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& recordingCallback
    ) const -> SubmissionResult;

private:
    ImmediateSubmissionQueue() = default;

    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    VkFence m_busyFence{VK_NULL_HANDLE};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};
};
} // namespace vkt