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
    bool enableAOFromFrontFace;
    bool enableAOFromBackFace;
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

    bool copyAOToOutputTexture;

    // When sampling gbuffer for albedo/specular, read as pure white vec4(1.0)
    bool gbufferWhiteOverride;
};

struct LightingPassResources
{
    // layout(rgba16, set = 0, binding = 0) uniform image2D image;
    //
    // layout(set = 1, binding = 0) uniform sampler2D gbuffer_Diffuse;
    // layout(set = 1, binding = 1) uniform sampler2D gbuffer_Specular;
    // layout(set = 1, binding = 2) uniform sampler2D gbuffer_Normal;
    // layout(set = 1, binding = 3) uniform sampler2D gbuffer_WorldPosition;
    // layout(set = 1, binding = 4) uniform sampler2D
    // gbuffer_OcclusionRoughnessMetallic;
    //
    // layout(r16, set = 2, binding = 0) uniform image2D inputAO;

    VkDescriptorSetLayout renderTargetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout gbufferLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout inputAOLayout{VK_NULL_HANDLE};

    std::unordered_map<size_t, VkShaderEXT> shaderBySpecializationHash{};
    VkPipelineLayout shaderLayout{VK_NULL_HANDLE};
};

struct SSAOPassResources
{
    // layout(r16, set = 0, binding = 0) uniform image2D outputAO;
    //
    // layout(set = 1, binding = 0) uniform sampler2D gbufferOccludee_Diffuse;
    // layout(set = 1, binding = 1) uniform sampler2D gbufferOccludee_Specular;
    // layout(set = 1, binding = 2) uniform sampler2D gbufferOccludee_Normal;
    // layout(set = 1, binding = 3) uniform sampler2D
    //     gbufferOccludee_WorldPosition;
    // layout(set = 1, binding = 4) uniform sampler2D gbufferOccludee_ORM;
    //
    // layout(rg16_snorm, set = 2, binding = 0) uniform image2D randomNormals;
    //
    // layout(set = 3, binding = 0) uniform sampler2D gbufferOccluder_Diffuse;
    // layout(set = 3, binding = 1) uniform sampler2D gbufferOccluder_Specular;
    // layout(set = 3, binding = 2) uniform sampler2D gbufferOccluder_Normal;
    // layout(set = 3, binding = 3) uniform sampler2D
    //     gbufferOccluder_WorldPosition;
    // layout(set = 3, binding = 4) uniform sampler2D gbufferOccluder_ORM;

    VkDescriptorSetLayout outputAOLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout gbufferOccludeeLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout randomNormalsLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout gbufferOccluderLayout{VK_NULL_HANDLE};

    VkDescriptorSet ambientOcclusionSet{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> ambientOcclusion{};

    VkDescriptorSet randomNormalsSet{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> randomNormals{};

    std::unordered_map<size_t, VkShaderEXT> shaderBySpecializationHash{};
    VkPipelineLayout shaderLayout{VK_NULL_HANDLE};
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
    recordDraw(VkCommandBuffer, RenderTarget&, GBuffer const& frontFace, GBuffer const& backFace, Scene const&);

    void controlsWindow(std::optional<ImGuiID> dockNode);

private:
    LightingPass() = default;

    VkDevice m_device{VK_NULL_HANDLE};
    std::unique_ptr<DescriptorAllocator> m_descriptorAllocator{};

    static LightingPassParameters DEFAULT_PARAMETERS;
    LightingPassParameters m_parameters{DEFAULT_PARAMETERS};

    LightingPassResources m_lightingPassResources{};
    SSAOPassResources m_ssaoPassResources{};
};
} // namespace vkt