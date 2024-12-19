#pragma once

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>
#include <optional>
#include <unordered_map>

namespace vkt
{
struct RenderTarget;
struct GBuffer;
struct Scene;
struct ImmediateSubmissionQueue;
} // namespace vkt

namespace vkt
{
struct LightingPassParameters
{
    bool enableAO;
    bool enableRandomNormalSampling;

    // When sampling a random normal to then reflect our AO samples, we sample
    // the XY since our samples are in 2D screen space
    // This produces a different effect, and normalizing is probably more
    // correct
    bool normalizeRandomNormals;

    glm::vec3 lightAxisAngles;
    float lightStrength;
    float ambientStrength;

    float occluderRadius;
    float occluderBias;
    float aoScale;

    // When sampling gbuffer, read diffuse and specular as pure white vec4(1.0)
    bool gbufferWhiteOverride;
};

struct LightingPass
{
    auto operator=(LightingPass&&) -> LightingPass&;
    LightingPass(LightingPass&&) noexcept;

    auto operator=(LightingPass const&) -> LightingPass& = delete;
    LightingPass(LightingPass const&) = delete;

    ~LightingPass();

    static auto create(VkDevice, VmaAllocator, ImmediateSubmissionQueue const&)
        -> std::optional<LightingPass>;

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

    // layout(rg16_snorm, set = 2, binding = 0) uniform image2D randomNormals;

    VkDescriptorSetLayout m_renderTargetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_gbufferLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_randomNormalsLayout{VK_NULL_HANDLE};

    VkDescriptorSet m_randomNormalsSet{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> m_randomNormals{};
    std::unique_ptr<DescriptorAllocator> m_descriptorAllocator{};

    static LightingPassParameters DEFAULT_PARAMETERS;
    LightingPassParameters m_parameters{DEFAULT_PARAMETERS};

    std::unordered_map<size_t, VkShaderEXT> m_shadersBySpecializationHash{};
    VkPipelineLayout m_shaderLayout{VK_NULL_HANDLE};
};
} // namespace vkt