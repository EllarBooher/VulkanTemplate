#pragma once

#include "vulkan_template/app/SceneTexture.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>
#include <vector>

namespace vkt
{
struct Renderer
{
public:
    auto operator=(Renderer&&) -> Renderer& = delete;
    Renderer(Renderer const&) = delete;
    auto operator=(Renderer const&) -> Renderer& = delete;

    Renderer(Renderer&&) noexcept;
    ~Renderer();

private:
    Renderer() = default;

public:
    static auto create(VkDevice) -> std::optional<Renderer>;

    void recordDraw(VkCommandBuffer, SceneTexture&) const;

private:
    VkDescriptorSetLayout m_destinationSingletonLayout{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkShaderEXT m_shader{VK_NULL_HANDLE};
    VkPipelineLayout m_shaderLayout{VK_NULL_HANDLE};
};
} // namespace vkt