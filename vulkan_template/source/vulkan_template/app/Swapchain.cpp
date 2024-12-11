#include "Swapchain.hpp"

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <tuple>
#include <utility>

namespace vkt
{
auto Swapchain::operator=(Swapchain&& other) noexcept -> Swapchain&
{
    destroy();

    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
    m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
    m_swapchain = std::exchange(other.m_swapchain, VK_NULL_HANDLE);
    m_imageFormat = std::exchange(other.m_imageFormat, VK_FORMAT_UNDEFINED);
    m_images = std::move(other.m_images);
    m_imageViews = std::move(other.m_imageViews);
    m_extent = std::exchange(other.m_extent, VkExtent2D{});

    return *this;
}
Swapchain::Swapchain(Swapchain&& other) noexcept { *this = std::move(other); }

Swapchain::~Swapchain() { destroy(); }

void Swapchain::destroy()
{
    if (m_device == VK_NULL_HANDLE)
    {
        // Check just one handle, since the lifetime of all members is the same
        if (m_swapchain != VK_NULL_HANDLE)
        {
            VKT_WARNING("Swapchain had allocations, but device was null. "
                        "Memory was possibly leaked.");
        }
        return;
    }

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

    for (VkImageView const view : m_imageViews)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }
}
} // namespace vkt

namespace
{
auto getBestFormat(
    VkPhysicalDevice const physicalDevice, VkSurfaceKHR const surface
) -> std::optional<VkSurfaceFormatKHR>
{
    uint32_t formatCount;
    VKT_TRY_VK(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice, surface, &formatCount, nullptr
        ),
        "Failed to query surface format support.",
        std::nullopt
    );

    std::vector<VkSurfaceFormatKHR> supportedFormats{formatCount};

    VKT_TRY_VK(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice, surface, &formatCount, supportedFormats.data()
        ),
        "Failed to query surface format support.",
        std::nullopt
    );

    std::optional<VkSurfaceFormatKHR> bestFormat{};

    std::vector<VkFormat> const formatPreferenceOrder{
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM
    };
    std::optional<size_t> bestFormatIndex{};

    for (auto const& format : supportedFormats)
    {
        if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            continue;
        }

        auto const formatIt = std::find(
            formatPreferenceOrder.begin(),
            formatPreferenceOrder.end(),
            format.format
        );

        if (formatIt == formatPreferenceOrder.end())
        {
            continue;
        }

        size_t const formatIndex{static_cast<size_t>(
            std::distance(formatPreferenceOrder.begin(), formatIt)
        )};

        // The format is supported, now we compare to the best we've found.

        if (!bestFormatIndex.has_value()
            || formatIndex < bestFormatIndex.value())
        {
            bestFormat = format;
            bestFormatIndex = formatIndex;
        }
    };

    return bestFormat;
}
} // namespace

namespace vkt
{
auto Swapchain::create(
    VkPhysicalDevice const physicalDevice,
    VkDevice const device,
    VkSurfaceKHR const surface,
    glm::u16vec2 const extent,
    std::optional<VkSwapchainKHR> const old
) -> std::optional<Swapchain>
{
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE
        || surface == VK_NULL_HANDLE)
    {
        VKT_ERROR("One or more necessary handles were null.");
        return std::nullopt;
    }

    std::optional<Swapchain> swapchainResult{std::in_place, Swapchain{}};
    Swapchain& swapchain{swapchainResult.value()};
    swapchain.m_device = device;
    swapchain.m_physicalDevice = physicalDevice;
    swapchain.m_surface = surface;

    std::optional<VkSurfaceFormatKHR> const surfaceFormatResult{
        getBestFormat(physicalDevice, surface)
    };
    if (!surfaceFormatResult.has_value())
    {
        VKT_ERROR("Could not find support for a suitable surface format.");
        return std::nullopt;
    }
    VkSurfaceFormatKHR const& surfaceFormat{surfaceFormatResult.value()};

    VKT_INFO(
        "Surface Format selected: Format: {}, ColorSpace: {}",
        string_VkFormat(surfaceFormat.format),
        string_VkColorSpaceKHR(surfaceFormat.colorSpace)
    );

    uint32_t const width{extent.x};
    uint32_t const height{extent.y};
    VkExtent2D const swapchainExtent{.width = width, .height = height};

    VkSwapchainCreateInfoKHR const swapchainCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,

        .flags = 0,
        .surface = surface,

        .minImageCount = 3,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    | VK_IMAGE_USAGE_STORAGE_BIT
                    | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,

        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,

        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = 1,
        .oldSwapchain = old.value_or(VK_NULL_HANDLE),
    };

    if (VkResult const swapchainResult{vkCreateSwapchainKHR(
            device, &swapchainCreateInfo, nullptr, &swapchain.m_swapchain
        )})
    {
        VKT_ERROR("Failed to create swapchain.");
        return std::nullopt;
    }

    swapchain.m_imageFormat = surfaceFormat.format;
    swapchain.m_extent = swapchainExtent;

    uint32_t swapchainImageCount{0};
    if (vkGetSwapchainImagesKHR(
            device, swapchain.m_swapchain, &swapchainImageCount, nullptr
        ) != VK_SUCCESS
        || swapchainImageCount == 0)
    {
        VKT_ERROR("Failed to get swapchain images.");
        return std::nullopt;
    }

    swapchain.m_images.resize(swapchainImageCount);
    if (vkGetSwapchainImagesKHR(
            device,
            swapchain.m_swapchain,
            &swapchainImageCount,
            swapchain.m_images.data()
        )
        != VK_SUCCESS)
    {
        VKT_ERROR("Failed to get swapchain images.");
        return std::nullopt;
    }

    for (size_t index{0}; index < swapchain.m_images.size(); index++)
    {
        VkImage const image{swapchain.m_images[index]};

        VkImageViewCreateInfo const viewInfo{imageViewCreateInfo(
            swapchain.m_imageFormat, image, VK_IMAGE_ASPECT_COLOR_BIT
        )};

        VkImageView view;
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
        {
            VKT_ERROR("Failed to create swapchain image view.");
            return std::nullopt;
        }
        swapchain.m_imageViews.push_back(view);
    }

    return swapchainResult;
}

auto Swapchain::swapchain() const -> VkSwapchainKHR { return m_swapchain; }

auto Swapchain::images() const -> std::span<VkImage const> { return m_images; }

auto Swapchain::imageViews() const -> std::span<VkImageView const>
{
    return m_imageViews;
}

auto Swapchain::extent() const -> VkExtent2D { return m_extent; }

auto Swapchain::rebuild() -> VkResult
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;

    VKT_PROPAGATE_VK(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_physicalDevice, m_surface, &surfaceCapabilities
        ),
        "Failed to get surface capabilities for swapchain creation."
    );

    glm::u16vec2 const newExtent{
        surfaceCapabilities.currentExtent.width,
        surfaceCapabilities.currentExtent.height
    };

    VKT_INFO(
        "Resizing swapchain: ({},{}) -> ({},{})",
        extent().width,
        extent().height,
        newExtent.x,
        newExtent.y
    );

    std::optional<Swapchain> newSwapchain{Swapchain::create(
        m_physicalDevice, m_device, m_surface, newExtent, m_swapchain
    )};
    if (!newSwapchain.has_value())
    {
        return VK_ERROR_UNKNOWN;
    }

    *this = std::move(newSwapchain).value();

    return VK_SUCCESS;
}

} // namespace vkt