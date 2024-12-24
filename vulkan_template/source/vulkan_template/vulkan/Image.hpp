#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/vec2.hpp>
#include <optional>
#include <span>
#include <vector>

namespace vkt
{
struct ImmediateSubmissionQueue;
} // namespace vkt

namespace vkt
{
struct ImageMemory
{
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};

    VmaAllocationCreateInfo allocationCreateInfo{};
    VmaAllocation allocation{VK_NULL_HANDLE};

    VkImageCreateInfo imageCreateInfo{};
    VkImage image{VK_NULL_HANDLE};
};

struct ImageAllocationParameters
{
    VkExtent2D extent{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageUsageFlags usageFlags{0};
    VkImageLayout initialLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageTiling tiling{VK_IMAGE_TILING_OPTIMAL};
    VmaMemoryUsage vmaUsage{VMA_MEMORY_USAGE_GPU_ONLY};
    VmaAllocationCreateFlags vmaFlags{0};
};

struct RGBATexel
{
    static uint8_t constexpr SATURATED_COMPONENT{255U};

    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{SATURATED_COMPONENT};
};

struct ImageRGBA
{
    uint32_t width{0};
    uint32_t height{0};
    std::vector<RGBATexel> texels{};
};

struct TexelRG16_SNORM
{
    int16_t r{0};
    int16_t g{0};
};

struct ImageRG16_SNORM
{
    glm::u32vec2 extent{};
    std::vector<TexelRG16_SNORM> texels{};
};

struct Image
{
public:
    auto operator=(Image const&) -> Image& = delete;
    Image(Image const&) = delete;

    auto operator=(Image&&) -> Image&;
    Image(Image&&) noexcept;

    ~Image();

private:
    Image() = default;
    void destroy();

public:
    static auto
    allocate(VkDevice, VmaAllocator, ImageAllocationParameters const&)
        -> std::optional<Image>;

    // The extent of the image will be derived from the passed CPU data.
    // Due to the requirements of uploading image data, usage flags will have
    // VK_IMAGE_USAGE_SAMPLED_BIT and VK_IMAGE_USAGE_TRANSFER_DST_BIT added.
    // Initial layout will be VK_IMAGE_LAYOUT_UNDEFINED,
    // and tiling will be VK_IMAGE_TILING_OPTIMAL.
    static auto uploadToDevice(
        VkDevice,
        VmaAllocator,
        vkt::ImmediateSubmissionQueue const&,
        VkFormat format,
        VkImageUsageFlags additionalFlags,
        ImageRGBA const& image
    ) -> std::optional<vkt::Image>;

    // Overload that takes in bytes. It is up to the caller to ensure that
    // extent/image/format are all synced and valid.
    static auto uploadToDevice(
        VkDevice,
        VmaAllocator,
        vkt::ImmediateSubmissionQueue const&,
        VkFormat format,
        VkImageUsageFlags additionalFlags,
        glm::u32vec2 extent,
        std::span<uint8_t const> bytes
    ) -> std::optional<vkt::Image>;

    // For now, all images are 2D (depth of 1)
    [[nodiscard]] auto extent3D() const -> VkExtent3D;
    [[nodiscard]] auto extent2D() const -> VkExtent2D;

    [[nodiscard]] auto aspectRatio() const -> std::optional<double>;
    [[nodiscard]] auto format() const -> VkFormat;

    // Returns the parameters used to create this image, useful for creating an
    // exact copy.
    [[nodiscard]] auto allocationParameters() const
        -> ImageAllocationParameters const&;

    // WARNING: Do not destroy this image. Be careful of implicit layout
    // transitions, which may break the guarantee of Image::expectedLayout.
    auto image() -> VkImage;
    auto fetchAllocationInfo() -> std::optional<VmaAllocationInfo>;

    [[nodiscard]] auto expectedLayout() const -> VkImageLayout;
    void recordTransitionBarriered(
        VkCommandBuffer, VkImageLayout dst, VkImageAspectFlags
    );
    void recordClearEntireColor(VkCommandBuffer, VkClearColorValue const*);

    // Assumes images are in TRANSFER_[DST/SRC]_OPTIMAL.
    static void recordCopyEntire(
        VkCommandBuffer, Image& src, Image& dst, VkImageAspectFlags
    );

    // Assumes images are in TRANSFER_[DST/SRC]_OPTIMAL.
    static void recordCopyRect(
        VkCommandBuffer,
        Image& src,
        Image& dst,
        VkImageAspectFlags,
        VkRect2D srcRect,
        VkRect2D dstRect
    );

private:
    ImageMemory m_memory{};
    VkImageLayout m_recordedLayout{};
    ImageAllocationParameters m_allocationParameters{};
};
} // namespace vkt