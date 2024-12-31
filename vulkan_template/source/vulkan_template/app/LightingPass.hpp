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

    glm::vec3 lightAxisAngles;
    float lightStrength;
    float ambientStrength;

    // When sampling gbuffer for albedo/specular, read as pure white vec4(1.0)
    bool gbufferWhiteOverride;

    // begin ambient occlusion settings

    bool enableAOFromFrontFace;
    bool enableAOFromBackFace;
    bool enableRandomNormalSampling;

    // When sampling a random normal to then reflect our AO samples, we sample
    // the XY since our samples are in 2D screen space
    // This produces a different effect, and normalizing is probably more
    // correct
    bool normalizeRandomNormals;

    float occluderRadius;
    float occluderBias;
    float aoScale;

    bool copyAOToOutputTexture;
    // Enables blurring the AO texture before using it for lighting
    bool blurAOTexture;

    // end ambient occlusion settings

    // begin shadow settings

    bool enableShadows;

    // Offset shadow reciever position by the normal, scaled by this factor
    float shadowReceiverPlaneDepthBias;
    // Subtract a constant bias from shadow receiver depth
    float shadowReceiverConstantBias;

    // Utilized for Vulkan fixed function depth computations
    float depthBiasConstant;
    float depthBiasSlope;

    float shadowNearPlane;
    float shadowFarPlane;

    // end shadow settings
};

struct ShadowmappingPassResources
{
    // Offscreen pass utilizing a vertex shader to collect depth values
    VkShaderEXT vertexShader{VK_NULL_HANDLE};
    VkPipelineLayout vertexShaderLayout{VK_NULL_HANDLE};
};

// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign)
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

    // A single combined sampler shadowmap, for the single global directional
    // light we utilize
    // see 'deferred/ligh.comp' for usage
    VkSampler shadowMapSampler{VK_NULL_HANDLE};
    VkDescriptorSetLayout shadowMapSetLayout{VK_NULL_HANDLE};
    VkDescriptorSet shadowMapSet{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> shadowMap{};

    std::unordered_map<size_t, VkShaderEXT> shaderBySpecializationHash{};
    VkPipelineLayout shaderLayout{VK_NULL_HANDLE};
};

// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign)
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

struct GaussianBlurPassResources
{
    // This blur is intended to be used on the AO image, to smooth the noise

    // layout(r16, set = 0, binding = 0) uniform readonly image2d imageInput;
    // layout(r16, set = 0, binding = 1) uniform writeonly image2d imageOut;

    VkDescriptorSetLayout inputOutputLayout{VK_NULL_HANDLE};

    VkDescriptorSet verticalBlurInputOutputSet{VK_NULL_HANDLE};
    VkDescriptorSet horizontalBlurInputOutputSet{VK_NULL_HANDLE};

    // TODO: figure out a better spot to hold these resources. Perhaps a array
    // in the out LightingPass object, where they can be created beforehand then
    // passed to be written into the inputOutputImageSet. This single image
    // layout is duplicated in a lot of places.

    std::unique_ptr<ImageView> halfBlurredImage{VK_NULL_HANDLE};

    // layout(r16, set = 0, binding = 0) uniform image2D;
    VkDescriptorSetLayout fullyBlurredImageLayout{VK_NULL_HANDLE};
    VkDescriptorSet fullyBlurredImageSet{VK_NULL_HANDLE};
    std::unique_ptr<ImageView> fullyBlurredImage{VK_NULL_HANDLE};

    VkShaderEXT verticalBlurShader{VK_NULL_HANDLE};
    VkShaderEXT horizontalBlurShader{VK_NULL_HANDLE};
    VkPipelineLayout blurLayout{VK_NULL_HANDLE};
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

    ShadowmappingPassResources m_shadowmappingPassResources{};
    LightingPassResources m_lightingPassResources{};
    SSAOPassResources m_ssaoPassResources{};
    GaussianBlurPassResources m_gaussianBlurPassResources{};
};
} // namespace vkt