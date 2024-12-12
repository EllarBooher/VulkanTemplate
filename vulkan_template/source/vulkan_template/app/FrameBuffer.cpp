#include "FrameBuffer.hpp"

#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/app/Swapchain.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageOperations.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <deque>
#include <functional>
#include <limits>
#include <span>
#include <utility>

namespace
{
struct DeletionQueue
{
public:
    void pushFunction(std::function<void()>&& function)
    {
        m_cleanupCallbacks.push_front(function);
    }

    void flush()
    {
        for (std::function<void()> const& function : m_cleanupCallbacks)
        {
            function();
        }

        m_cleanupCallbacks.clear();
    }
    void clear() { m_cleanupCallbacks.clear(); }

    ~DeletionQueue() noexcept
    {
        if (!m_cleanupCallbacks.empty())
        {
            VKT_WARNING(
                "Cleanup callbacks was flushed, potentially indicating "
                "that dealing with a DeletionQueue instance was forgotten."
            );
            flush();
        }
    }

private:
    std::deque<std::function<void()>> m_cleanupCallbacks{};
};

auto createFrame(VkDevice const device, uint32_t const queueFamilyIndex)
    -> std::optional<vkt::Frame>
{
    std::optional<vkt::Frame> frameResult{std::in_place};
    vkt::Frame& frame{frameResult.value()};

    DeletionQueue cleanupCallbacks{};
    cleanupCallbacks.pushFunction([&]() { frame.destroy(device); });

    VkCommandPoolCreateInfo const commandPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    };

    if (VkResult const result{vkCreateCommandPool(
            device, &commandPoolInfo, nullptr, &frame.commandPool
        )};
        result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to allocate frame command pool.");
        cleanupCallbacks.flush();
        return std::nullopt;
    }

    VkCommandBufferAllocateInfo const cmdAllocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = frame.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (VkResult const result{vkAllocateCommandBuffers(
            device, &cmdAllocInfo, &frame.mainCommandBuffer
        )};
        result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to allocate frame command buffer.");
        cleanupCallbacks.flush();
        return std::nullopt;
    }

    // Frames start signaled so they can be initially used
    VkFenceCreateInfo const fenceCreateInfo{
        vkt::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT)
    };

    if (VkResult const result{
            vkCreateFence(device, &fenceCreateInfo, nullptr, &frame.renderFence)
        };
        result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to allocate frame in-use fence.");
        cleanupCallbacks.flush();
        return std::nullopt;
    }

    VkSemaphoreCreateInfo const semaphoreCreateInfo{vkt::semaphoreCreateInfo()};

    if (VkResult const result{vkCreateSemaphore(
            device, &semaphoreCreateInfo, nullptr, &frame.swapchainSemaphore
        )};
        result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to allocate frame swapchain semaphore.");
        cleanupCallbacks.flush();
        return std::nullopt;
    }

    if (VkResult const result{vkCreateSemaphore(
            device, &semaphoreCreateInfo, nullptr, &frame.renderSemaphore
        )};
        result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to allocate frame render semaphore.");
        cleanupCallbacks.flush();
        return std::nullopt;
    }

    cleanupCallbacks.clear();
    return frameResult;
}
} // namespace

namespace vkt
{
void Frame::destroy(VkDevice const device)
{
    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyFence(device, renderFence, nullptr);
    vkDestroySemaphore(device, renderSemaphore, nullptr);
    vkDestroySemaphore(device, swapchainSemaphore, nullptr);

    *this = Frame{};
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_frames = std::move(other.m_frames);
    m_frameNumber = std::exchange(other.m_frameNumber, 0);
}

FrameBuffer::~FrameBuffer() { destroy(); }

auto FrameBuffer::create(VkDevice const device, uint32_t const queueFamilyIndex)
    -> std::optional<FrameBuffer>
{
    if (device == VK_NULL_HANDLE)
    {
        VKT_ERROR("Device is null.");
        return std::nullopt;
    }

    std::optional<FrameBuffer> frameBufferResult{std::in_place, FrameBuffer{}};
    FrameBuffer& frameBuffer{frameBufferResult.value()};
    frameBuffer.m_device = device;

    size_t constexpr FRAMES_IN_FLIGHT{2};

    for (size_t i{0}; i < FRAMES_IN_FLIGHT; i++)
    {
        std::optional<Frame> const frameResult{
            createFrame(device, queueFamilyIndex)
        };
        if (!frameResult.has_value())
        {
            VKT_ERROR("Failed to allocate frame for framebuffer.");
            return std::nullopt;
        }
        frameBuffer.m_frames.push_back(frameResult.value());
    }

    return frameBufferResult;
}

auto FrameBuffer::beginNewFrame() -> VkResult
{
    m_frameNumber++;
    Frame const& frame{currentFrame()};

    uint64_t constexpr FRAME_WAIT_TIMEOUT_NANOSECONDS = 1'000'000'000;
    if (VkResult const waitResult{vkWaitForFences(
            m_device,
            1,
            &frame.renderFence,
            VK_TRUE,
            FRAME_WAIT_TIMEOUT_NANOSECONDS
        )};
        waitResult != VK_SUCCESS)
    {
        VKT_LOG_VK(waitResult, "Failed to wait on frame in-use fence.");
        return waitResult;
    }

    if (VkResult const resetResult{
            vkResetFences(m_device, 1, &frame.renderFence)
        };
        resetResult != VK_SUCCESS)
    {
        VKT_LOG_VK(resetResult, "Failed to reset frame fences.");
        return resetResult;
    }

    if (VkResult const resetCmdResult{
            vkResetCommandBuffer(frame.mainCommandBuffer, 0)
        };
        resetCmdResult != VK_SUCCESS)
    {
        VKT_LOG_VK(resetCmdResult, "Failed to reset frame command buffer.");
        return resetCmdResult;
    }

    VkCommandBufferBeginInfo const cmdBeginInfo{
        commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
    };
    if (VkResult const beginCmdResult{
            vkBeginCommandBuffer(frame.mainCommandBuffer, &cmdBeginInfo)
        };
        beginCmdResult != VK_SUCCESS)
    {
        VKT_LOG_VK(beginCmdResult, "Failed to begin frame command buffer.");
        return beginCmdResult;
    }

    return VK_SUCCESS;
}

auto FrameBuffer::currentFrame() const -> Frame const&
{
    size_t const index{m_frameNumber % m_frames.size()};
    return m_frames[index];
}

auto FrameBuffer::finishFrameWithPresent(
    Swapchain& swapchain,
    VkQueue const submissionQueue,
    RenderTarget& sourceTexture
) -> VkResult
{
    // Copy image to swapchain
    uint64_t constexpr ACQUIRE_TIMEOUT_NANOSECONDS = 1'000'000'000;

    uint32_t swapchainImageIndex{std::numeric_limits<uint32_t>::max()};

    Frame const& frame{currentFrame()};
    VkCommandBuffer const cmd{frame.mainCommandBuffer};

    if (VkResult const acquireResult{vkAcquireNextImageKHR(
            m_device,
            swapchain.swapchain(),
            ACQUIRE_TIMEOUT_NANOSECONDS,
            currentFrame().swapchainSemaphore,
            VK_NULL_HANDLE // No Fence to signal
            ,
            &swapchainImageIndex
        )};
        acquireResult != VK_SUCCESS)
    {
        if (acquireResult != VK_ERROR_OUT_OF_DATE_KHR)
        {
            VKT_LOG_VK(acquireResult, "Failed to acquire swapchain image.");
        }
        VKT_LOG_VK(vkEndCommandBuffer(cmd), "Failed to end command buffer.");
        return acquireResult;
    }

    sourceTexture.color().recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );

    VkImage const swapchainImage{swapchain.images()[swapchainImageIndex]};
    transitionImage(
        cmd,
        swapchainImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    recordCopyImageToImage(
        cmd,
        sourceTexture.color().image().image(),
        swapchainImage,
        sourceTexture.size(),
        VkRect2D{.extent{swapchain.extent()}}
    );

    transitionImage(
        cmd,
        swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VKT_PROPAGATE_VK(
        vkEndCommandBuffer(cmd),
        "Failed to end command buffer after recording copy into swapchain."
    );

    // Submit commands

    VkCommandBufferSubmitInfo const cmdSubmitInfo{commandBufferSubmitInfo(cmd)};
    VkSemaphoreSubmitInfo const waitInfo{semaphoreSubmitInfo(
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, frame.swapchainSemaphore
    )};
    VkSemaphoreSubmitInfo const signalInfo{semaphoreSubmitInfo(
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, frame.renderSemaphore
    )};

    std::vector<VkCommandBufferSubmitInfo> const cmdSubmitInfos{cmdSubmitInfo};
    std::vector<VkSemaphoreSubmitInfo> const waitInfos{waitInfo};
    std::vector<VkSemaphoreSubmitInfo> const signalInfos{signalInfo};

    // TODO: transferring to the swapchain should have its own command buffer
    VkSubmitInfo2 const submission =
        submitInfo(cmdSubmitInfos, waitInfos, signalInfos);

    VKT_PROPAGATE_VK(
        vkQueueSubmit2(submissionQueue, 1, &submission, frame.renderFence),
        "Failed to submit command buffer before frame presentation."
    );

    VkSwapchainKHR const swapchainHandle{swapchain.swapchain()};
    VkPresentInfoKHR const presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,

        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.renderSemaphore,

        .swapchainCount = 1,
        .pSwapchains = &swapchainHandle,

        .pImageIndices = &swapchainImageIndex,
        .pResults = nullptr, // Only one swapchain
    };

    if (VkResult const presentResult{
            vkQueuePresentKHR(submissionQueue, &presentInfo)
        };
        presentResult != VK_SUCCESS)
    {
        if (presentResult != VK_ERROR_OUT_OF_DATE_KHR)
        {
            VKT_LOG_VK(
                presentResult,
                "Failed swapchain presentation due to error that was not "
                "OUT_OF_DATE."
            );
        }
        return presentResult;
    }

    return VK_SUCCESS;
}

auto FrameBuffer::frameNumber() const -> size_t { return m_frameNumber; }

void FrameBuffer::destroy()
{
    if (m_device == VK_NULL_HANDLE)
    {
        if (!m_frames.empty())
        {
            VKT_WARNING("FrameBuffer destroyed with no device, but allocated "
                        "frames. Memory was maybe leaked.");
        }
        return;
    }

    for (Frame& frame : m_frames)
    {
        frame.destroy(m_device);
    }

    m_device = VK_NULL_HANDLE;
    m_frames.clear();
    m_frameNumber = 0;
}
} // namespace vkt