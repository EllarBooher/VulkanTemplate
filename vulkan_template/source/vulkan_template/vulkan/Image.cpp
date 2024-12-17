#include "Image.hpp"

#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/ImageOperations.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <algorithm>
#include <functional>
#include <spdlog/fmt/bundled/core.h>
#include <spdlog/fmt/bundled/format.h>
#include <utility>
#include <vector>

namespace vkt
{
Image::Image(Image&& other) noexcept
{
    m_memory = std::exchange(other.m_memory, ImageMemory{});
}

Image::~Image() { destroy(); }

void Image::destroy()
{
    bool leaked{false};
    if (m_memory.allocation != VK_NULL_HANDLE)
    {
        if (m_memory.allocator != VK_NULL_HANDLE)
        {
            vmaDestroyImage(
                m_memory.allocator, m_memory.image, m_memory.allocation
            );
        }
        else
        {
            leaked = true;
        }
    }
    else if (m_memory.image != VK_NULL_HANDLE)
    {
        if (m_memory.device == VK_NULL_HANDLE)
        {
            vkDestroyImage(m_memory.device, m_memory.image, nullptr);
        }
        else
        {
            leaked = true;
        }
    }

    if (leaked)
    {
        VKT_WARNING(fmt::format(
            "Leak detected in image. Allocator: {}. "
            "Allocation: {}. Device: {}. VkImage: {}.",
            fmt::ptr(m_memory.allocator),
            fmt::ptr(m_memory.allocation),
            fmt::ptr(m_memory.device),
            fmt::ptr(m_memory.image)
        ));
    }

    m_memory = ImageMemory{};
    m_recordedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

auto Image::allocate(
    VkDevice const device,
    VmaAllocator const allocator,
    ImageAllocationParameters const& parameters
) -> std::optional<Image>
{
    VkExtent3D const extent3D{
        .width = parameters.extent.width,
        .height = parameters.extent.height,
        .depth = 1,
    };

    VkImageCreateInfo const imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .imageType = VK_IMAGE_TYPE_2D,

        .format = parameters.format,
        .extent = extent3D,

        .mipLevels = 1,
        .arrayLayers = 1,

        .samples = VK_SAMPLE_COUNT_1_BIT,

        .tiling = parameters.tiling,
        .usage = parameters.usageFlags,

        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,

        .initialLayout = parameters.initialLayout,
    };

    VmaAllocationCreateInfo const imageAllocInfo{
        .flags = parameters.vmaFlags,
        .usage = parameters.vmaUsage,
    };

    std::optional<Image> imageResult{std::in_place, Image{}};
    Image& image{imageResult.value()};

    VkImage imageHandle;
    VmaAllocation allocation;
    VkResult const createImageResult{vmaCreateImage(
        allocator,
        &imageInfo,
        &imageAllocInfo,
        &imageHandle,
        &allocation,
        nullptr
    )};
    if (createImageResult != VK_SUCCESS)
    {
        VKT_LOG_VK(createImageResult, "VMA Allocation for image failed.");
        return std::nullopt;
    }

    image.m_memory = ImageMemory{
        .device = device,
        .allocator = allocator,
        .allocationCreateInfo = imageAllocInfo,
        .allocation = allocation,
        .imageCreateInfo = imageInfo,
        .image = imageHandle,
    };

    image.m_recordedLayout = imageInfo.initialLayout;

    return imageResult;
}

auto Image::uploadToDevice(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue,
    VkFormat const format,
    VkImageUsageFlags const additionalFlags,
    ImageRGBA const& image
) -> std::optional<vkt::Image>
{
    VkExtent2D const imageExtent{.width = image.x, .height = image.y};

    std::optional<vkt::Image> stagingImageResult{vkt::Image::allocate(
        device,
        allocator,
        vkt::ImageAllocationParameters{
            .extent = imageExtent,
            .format = format,
            .usageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .vmaUsage = VMA_MEMORY_USAGE_CPU_ONLY,
            .vmaFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        }
    )};
    if (!stagingImageResult.has_value())
    {
        VKT_ERROR("Failed to allocate staging image.");
        return std::nullopt;
    }
    vkt::Image& stagingImage{stagingImageResult.value()};

    std::optional<VmaAllocationInfo> const allocationInfo{
        stagingImage.fetchAllocationInfo()
    };

    if (allocationInfo.has_value()
        && allocationInfo.value().pMappedData != nullptr)
    {
        auto* const stagingImageData{
            reinterpret_cast<uint8_t*>(allocationInfo.value().pMappedData)
        };

        std::copy(image.bytes.begin(), image.bytes.end(), stagingImageData);
    }
    else
    {
        VKT_ERROR("Failed to map bytes of staging image.");
        return std::nullopt;
    }

    std::optional<vkt::Image> finalImageResult{vkt::Image::allocate(
        device,
        allocator,
        vkt::ImageAllocationParameters{
            .extent = imageExtent,
            .format = format,
            .usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT | additionalFlags,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .tiling = VK_IMAGE_TILING_OPTIMAL
        }
    )};
    if (!finalImageResult.has_value())
    {
        VKT_ERROR("Failed to allocate final image.");
        return std::nullopt;
    }
    vkt::Image& finalImage{finalImageResult.value()};

    if (auto const submissionResult{submissionQueue.immediateSubmit(
            [&](VkCommandBuffer const cmd)
    {
        stagingImage.recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT
        );

        finalImage.recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT
        );

        vkt::Image::recordCopyEntire(
            cmd, stagingImage, finalImage, VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
        )};
        submissionResult
        != vkt::ImmediateSubmissionQueue::SubmissionResult::SUCCESS)
    {
        VKT_ERROR("Failed to copy images.");
        return std::nullopt;
    }

    return std::move(finalImageResult).value();
}

auto Image::extent3D() const -> VkExtent3D
{
    return m_memory.imageCreateInfo.extent;
}

auto Image::extent2D() const -> VkExtent2D
{
    return VkExtent2D{.width = extent3D().width, .height = extent3D().height};
}

auto Image::aspectRatio() const -> std::optional<double>
{
    return vkt::aspectRatio(extent2D());
}

auto Image::format() const -> VkFormat
{
    return m_memory.imageCreateInfo.format;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
auto Image::image() -> VkImage { return m_memory.image; }

// NOLINTNEXTLINE(readability-make-member-function-const)
auto Image::fetchAllocationInfo() -> std::optional<VmaAllocationInfo>
{
    if (m_memory.allocator == VK_NULL_HANDLE)
    {
        return std::nullopt;
    }
    VmaAllocationInfo allocationInfo;
    vmaGetAllocationInfo(
        m_memory.allocator, m_memory.allocation, &allocationInfo
    );
    return allocationInfo;
}

auto Image::expectedLayout() const -> VkImageLayout { return m_recordedLayout; }

void Image::recordTransitionBarriered(
    VkCommandBuffer const cmd,
    VkImageLayout const dst,
    VkImageAspectFlags const aspectMask
)
{
    transitionImage(cmd, m_memory.image, m_recordedLayout, dst, aspectMask);

    m_recordedLayout = dst;
}

void Image::recordClearEntireColor(
    VkCommandBuffer const cmd, VkClearColorValue const* pColor
)
{
    std::vector<VkImageSubresourceRange> ranges{
        imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT)
    };
    vkCmdClearColorImage(
        cmd, m_memory.image, m_recordedLayout, pColor, VKR_ARRAY(ranges)
    );
}

void Image::recordCopyEntire(
    VkCommandBuffer const cmd,
    Image& src,
    Image& dst,
    VkImageAspectFlags const aspectMask
)
{
    recordCopyImageToImage(
        cmd,
        src.image(),
        dst.image(),
        aspectMask,
        src.extent3D(),
        dst.extent3D()
    );
}

void Image::recordCopyRect(
    VkCommandBuffer const cmd,
    Image& src,
    Image& dst,
    VkImageAspectFlags const aspectMask,
    VkOffset3D const srcMin,
    VkOffset3D const srcMax,
    VkOffset3D const dstMin,
    VkOffset3D const dstMax
)
{
    recordCopyImageToImage(
        cmd,
        src.image(),
        dst.image(),
        aspectMask,
        srcMin,
        srcMax,
        dstMin,
        dstMax
    );
}
} // namespace vkt