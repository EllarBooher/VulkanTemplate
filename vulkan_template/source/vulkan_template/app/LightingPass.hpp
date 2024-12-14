#pragma once

#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>

namespace vkt
{
struct RenderTarget;
struct GBuffer;
} // namespace vkt

namespace vkt
{
struct LightingPass
{
    auto operator=(LightingPass&&) -> LightingPass&;
    LightingPass(LightingPass&&) noexcept;

    auto operator=(LightingPass const&) -> LightingPass& = delete;
    LightingPass(LightingPass const&) = delete;

    ~LightingPass();

    static auto create(VkDevice) -> std::optional<LightingPass>;

    void recordDraw(VkCommandBuffer, RenderTarget&, GBuffer const&);

private:
    LightingPass() = default;

    VkDevice m_device{VK_NULL_HANDLE};

    // layout(rgba16, set = 0, binding = 0) uniform image2D image;
    //
    // layout(set = 1, binding = 0) uniform sampler2D gbuffer_Diffuse;
    // layout(set = 1, binding = 1) uniform sampler2D gbuffer_Specular;
    // layout(set = 1, binding = 2) uniform sampler2D gbuffer_Normal;
    // layout(set = 1, binding = 3) uniform sampler2D gbuffer_WorldPosition;
    // layout(set = 1, binding = 4) uniform sampler2D
    // gbuffer_OcclusionRoughnessMetallic;

    VkDescriptorSetLayout m_renderTargetLayout{};
    VkDescriptorSetLayout m_gbufferLayout{};

    VkShaderEXT m_shader{VK_NULL_HANDLE};
    VkPipelineLayout m_shaderLayout{VK_NULL_HANDLE};
};
} // namespace vkt