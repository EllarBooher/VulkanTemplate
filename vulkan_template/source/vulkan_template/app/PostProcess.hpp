#pragma once

#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>

namespace vkt
{
struct RenderTarget;
} // namespace vkt

namespace vkt
{
struct PostProcess
{
    auto operator=(PostProcess&&) -> PostProcess&;
    PostProcess(PostProcess&&) noexcept;

    auto operator=(PostProcess const&) -> PostProcess& = delete;
    PostProcess(PostProcess const&) = delete;

    ~PostProcess();

    static auto create(VkDevice) -> std::optional<PostProcess>;

    // Assumes the input texture is linearly encoded. Schedules compute work to
    // in-place convert to nonlinear SRGB encoding.
    void recordLinearToSRGB(VkCommandBuffer, RenderTarget&);

private:
    PostProcess() = default;

    VkDevice m_device{VK_NULL_HANDLE};

    VkDescriptorSetLayout m_transferSingletonLayout{};
    VkShaderEXT m_oetfSRGB{VK_NULL_HANDLE};
    VkPipelineLayout m_oetfSRGBLayout{VK_NULL_HANDLE};
};
} // namespace vkt