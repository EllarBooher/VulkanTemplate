#pragma once

#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/vec3.hpp>
#include <imgui.h>
#include <optional>

namespace vkt
{
struct RenderTarget;
struct GBuffer;
struct Scene;
} // namespace vkt

namespace vkt
{
struct LightingPassParameters
{
    bool enableAO;

    glm::vec3 lightAxisAngles;
    float lightStrength;
    float ambientStrength;

    float occluderRadius;
    float occluderBias;
    float aoScale;
};

struct LightingPass
{
    auto operator=(LightingPass&&) -> LightingPass&;
    LightingPass(LightingPass&&) noexcept;

    auto operator=(LightingPass const&) -> LightingPass& = delete;
    LightingPass(LightingPass const&) = delete;

    ~LightingPass();

    static auto create(VkDevice) -> std::optional<LightingPass>;

    void
    recordDraw(VkCommandBuffer, RenderTarget&, GBuffer const&, Scene const&);

    void controlsWindow(std::optional<ImGuiID> dockNode);

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

    static LightingPassParameters DEFAULT_PARAMETERS;
    LightingPassParameters m_parameters{DEFAULT_PARAMETERS};

    VkShaderEXT m_shaderWithoutAO{VK_NULL_HANDLE};
    VkShaderEXT m_shaderWithAO{VK_NULL_HANDLE};
    VkPipelineLayout m_shaderLayout{VK_NULL_HANDLE};
};
} // namespace vkt