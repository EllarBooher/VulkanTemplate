#pragma once

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <memory>
#include <optional>

namespace vkt
{
struct RenderTarget
{
    RenderTarget(RenderTarget const&) = delete;
    auto operator=(RenderTarget const&) -> RenderTarget& = delete;

    RenderTarget(RenderTarget&&) noexcept;
    auto operator=(RenderTarget&&) noexcept -> RenderTarget&;

    ~RenderTarget();

    struct CreateParameters
    {
        VkExtent2D max;
        VkFormat color;
        VkFormat depth;
    };

    // The texture is allocated once. It is expected to render into a portion of
    // it, so windows can be resized without reallocation.
    // Thus the texture should be large enough to handle as large as the window
    // is expected to get.
    static auto create(VkDevice, VmaAllocator, CreateParameters)
        -> std::optional<RenderTarget>;

    [[nodiscard]] auto colorSampler() const -> VkSampler;

    auto color() -> ImageView&;
    [[nodiscard]] auto color() const -> ImageView const&;
    auto depth() -> ImageView&;
    [[nodiscard]] auto depth() const -> ImageView const&;

    // layout(binding = 0) uniform image2D image;

    [[nodiscard]] auto singletonDescriptor() const -> VkDescriptorSet;
    [[nodiscard]] auto singletonLayout() const -> VkDescriptorSetLayout;
    static auto allocateSingletonLayout(VkDevice)
        -> std::optional<VkDescriptorSetLayout>;

    // layout(binding = 0) uniform image2D image;
    // layout(binding = 1) uniform sampler2D fragmentDepth;

    [[nodiscard]] auto combinedDescriptor() const -> VkDescriptorSet;
    [[nodiscard]] auto combinedDescriptorLayout() const
        -> VkDescriptorSetLayout;
    static auto allocateCombinedLayout(VkDevice)
        -> std::optional<VkDescriptorSetLayout>;

    void setSize(VkRect2D);
    [[nodiscard]] auto size() const -> VkRect2D;

private:
    RenderTarget() = default;

    void destroy() noexcept;

    // Indicates which pixels are valid out of the full allocated capacity.
    VkRect2D m_size{};

    // The device used to create this.
    VkDevice m_device{VK_NULL_HANDLE};

    std::unique_ptr<DescriptorAllocator> m_descriptorPool{};

    VkSampler m_colorSampler{VK_NULL_HANDLE};
    VkSampler m_depthSampler{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> m_color{};
    std::unique_ptr<ImageView> m_depth{};

    VkDescriptorSetLayout m_singletonDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_singletonDescriptor{VK_NULL_HANDLE};

    VkDescriptorSetLayout m_combinedDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_combinedDescriptor{VK_NULL_HANDLE};
};
} // namespace vkt