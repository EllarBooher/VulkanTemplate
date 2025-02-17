#include "ImageView.hpp"

#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include <spdlog/fmt/bundled/core.h>
#include <spdlog/fmt/bundled/format.h>
#include <utility>

namespace vkt
{
ImageView::ImageView(ImageView&& other) noexcept
{
    m_image = std::move(other.m_image);
    m_memory = std::exchange(other.m_memory, ImageViewMemory{});
}

ImageView::~ImageView() { destroy(); }

auto ImageView::allocate(
    VkDevice const device,
    VmaAllocator const allocator,
    ImageAllocationParameters const& imageParameters,
    ImageViewAllocationParameters const& viewParameters
) -> std::optional<std::unique_ptr<ImageView>>
{
    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
    {
        VKT_ERROR("Device or allocator were null.");
        return std::nullopt;
    }

    std::optional<std::unique_ptr<Image>> imageAllocationResult{
        Image::allocate(device, allocator, imageParameters)
    };
    if (!imageAllocationResult.has_value()
        || imageAllocationResult.value() == nullptr)
    {
        VKT_ERROR("Failed to allocate Image.");
        return std::nullopt;
    }

    return allocate(
        device,
        allocator,
        std::move(*imageAllocationResult.value()),
        viewParameters
    );
}

auto ImageView::allocate(
    VkDevice const device,
    VmaAllocator const allocator,
    Image&& preallocatedImage,
    ImageViewAllocationParameters const& viewParameters
) -> std::optional<std::unique_ptr<ImageView>>
{
    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
    {
        VKT_ERROR("Device or allocator were null.");
        return std::nullopt;
    }

    ImageView finalView{};
    finalView.m_image = std::make_unique<Image>(std::move(preallocatedImage));

    Image& image{*finalView.m_image};

    VkImageViewCreateInfo const imageViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,

        .flags = viewParameters.flags,

        .image = image.image(),
        .viewType = viewParameters.viewType,
        .format = viewParameters.formatOverride.value_or(image.format()),
        .components = viewParameters.components,
        .subresourceRange = viewParameters.subresourceRange,
    };

    VkImageView view;
    VKT_TRY_VK(
        vkCreateImageView(device, &imageViewInfo, nullptr, &view),
        "Failed to create VkImageView.",
        std::nullopt
    );

    finalView.m_memory = ImageViewMemory{
        .device = device,
        .viewCreateInfo = imageViewInfo,
        .view = view,
    };

    return std::make_unique<ImageView>(std::move(finalView));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
auto ImageView::view() -> VkImageView { return m_memory.view; }

auto ImageView::image() -> Image& { return *m_image; }

auto ImageView::image() const -> Image const& { return *m_image; }

void ImageView::recordTransitionBarriered(
    VkCommandBuffer const cmd, VkImageLayout const dst
)
{
    image().recordTransitionBarriered(
        cmd, dst, m_memory.viewCreateInfo.subresourceRange.aspectMask
    );
}

auto ImageView::expectedLayout() const -> VkImageLayout
{
    return m_image != nullptr ? m_image->expectedLayout()
                              : VK_IMAGE_LAYOUT_UNDEFINED;
}

void ImageView::destroy()
{
    bool leaked{false};
    if (m_memory.view != VK_NULL_HANDLE)
    {
        if (m_memory.device != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_memory.device, m_memory.view, nullptr);
        }
        else
        {
            leaked = true;
        }
    }

    if (leaked)
    {
        VKT_WARNING(fmt::format(
            "Leak detected in image view. Device: {}. VkImageView: {}.",
            fmt::ptr(m_memory.device),
            fmt::ptr(m_memory.view)
        ));
    }

    m_image.reset();
    m_memory = ImageViewMemory{};
}
} // namespace vkt