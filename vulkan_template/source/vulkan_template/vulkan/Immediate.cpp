#include "Immediate.hpp"

#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <cassert>
#include <utility>
#include <vector>

namespace vkt
{
auto ImmediateSubmissionQueue::operator=(ImmediateSubmissionQueue&& other
) noexcept -> ImmediateSubmissionQueue&
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_queue = std::exchange(other.m_queue, VK_NULL_HANDLE);
    m_busyFence = std::exchange(other.m_busyFence, VK_NULL_HANDLE);
    m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
    m_commandBuffer = std::exchange(other.m_commandBuffer, VK_NULL_HANDLE);

    return *this;
}

ImmediateSubmissionQueue::ImmediateSubmissionQueue(
    ImmediateSubmissionQueue&& other
) noexcept
{
    *this = std::move(other);
}

ImmediateSubmissionQueue::~ImmediateSubmissionQueue()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }

    vkDestroyFence(m_device, m_busyFence, nullptr);
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
}

auto ImmediateSubmissionQueue::create(
    VkDevice const device, VkQueue const queue, uint32_t const queueFamilyIndex
) -> std::optional<ImmediateSubmissionQueue>
{
    std::optional<ImmediateSubmissionQueue> result{
        std::in_place, ImmediateSubmissionQueue{}
    };
    ImmediateSubmissionQueue& immQueue{result.value()};
    immQueue.m_device = device;
    immQueue.m_queue = queue;

    VkCommandPoolCreateInfo const commandPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    };

    VKT_TRY_VK(
        vkCreateCommandPool(
            device, &commandPoolInfo, nullptr, &immQueue.m_commandPool
        ),
        "Failed to allocate command pool.",
        std::nullopt
    );

    VkCommandBufferAllocateInfo const commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,

        .commandPool = immQueue.m_commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VKT_TRY_VK(
        vkAllocateCommandBuffers(
            device, &commandBufferInfo, &immQueue.m_commandBuffer
        ),
        "Failed to allocate command buffers.",
        std::nullopt
    );

    VkFenceCreateInfo const fenceInfo{
        fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT)
    };

    VKT_TRY_VK(
        vkCreateFence(device, &fenceInfo, nullptr, &immQueue.m_busyFence),
        "Failed to create fence.",
        std::nullopt
    );

    return result;
}

auto ImmediateSubmissionQueue::immediateSubmit(
    std::function<void(VkCommandBuffer cmd)>&& recordingCallback
) const -> SubmissionResult
{
    assert(
        m_device != VK_NULL_HANDLE
        && "Immediate submission queue not initialized."
    );

    VKT_TRY_VK(
        vkResetFences(m_device, 1, &m_busyFence),
        "Failed to reset fences",
        SubmissionResult::FAILED
    );
    VKT_TRY_VK(
        vkResetCommandBuffer(m_commandBuffer, 0),
        "Failed to reset command buffer",
        SubmissionResult::FAILED
    );

    VkCommandBufferBeginInfo const cmdBeginInfo{
        commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
    };

    VKT_TRY_VK(
        vkBeginCommandBuffer(m_commandBuffer, &cmdBeginInfo),
        "Failed to begin command buffer",
        SubmissionResult::FAILED
    );

    recordingCallback(m_commandBuffer);

    VKT_TRY_VK(
        vkEndCommandBuffer(m_commandBuffer),
        "Failed to end command buffer",
        SubmissionResult::FAILED
    );

    VkCommandBufferSubmitInfo const cmdSubmitInfo{
        commandBufferSubmitInfo(m_commandBuffer)
    };
    std::vector<VkCommandBufferSubmitInfo> const cmdSubmitInfos{cmdSubmitInfo};
    VkSubmitInfo2 const info{submitInfo(cmdSubmitInfos, {}, {})};

    VKT_TRY_VK(
        vkQueueSubmit2(m_queue, 1, &info, m_busyFence),
        "Failed to submit command buffer",
        SubmissionResult::FAILED
    );

    // 1 second timeout
    uint64_t constexpr SUBMIT_TIMEOUT_NANOSECONDS{1'000'000'000};
    VkBool32 constexpr WAIT_ALL{VK_TRUE};
    auto const waitResult{vkWaitForFences(
        m_device, 1, &m_busyFence, WAIT_ALL, SUBMIT_TIMEOUT_NANOSECONDS
    )};

    switch (waitResult)
    {
    case VK_SUCCESS:
        return SubmissionResult::SUCCESS;
        break;
    case VK_TIMEOUT:
        return SubmissionResult::TIMEOUT;
        break;
    default:
        VKT_LOG_VK(
            waitResult, "Failed to wait on fences with unexpected error"
        );
        return SubmissionResult::FAILED;
        break;
    }
}
} // namespace vkt