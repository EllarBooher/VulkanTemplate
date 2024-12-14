#pragma once

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace vkt
{
struct Scene;
struct RenderTarget;
} // namespace vkt

namespace vkt
{
struct GBufferTextures
{
    std::unique_ptr<ImageView> diffuse;
    std::unique_ptr<ImageView> specular;
    std::unique_ptr<ImageView> normal;
    std::unique_ptr<ImageView> worldPosition;
    std::unique_ptr<ImageView> occlusionRoughnessMetallic;
};

enum class GBufferTextureIndices : size_t
{
    Diffuse = 0,
    Specular = 1,
    Normal = 2,
    WorldPosition = 3,
    ORM = 4,
    COUNT = 5
};

struct GBuffer
{
public:
    auto operator=(GBuffer&&) -> GBuffer&;
    GBuffer(GBuffer&&);

    auto operator=(GBuffer const&) -> GBuffer& = delete;
    GBuffer(GBuffer const&) = delete;

    ~GBuffer();

private:
    GBuffer() = default;

public:
    static auto create(VkDevice, VmaAllocator, VkExtent2D capacity)
        -> std::optional<GBuffer>;

    // layout(set = n, binding = 0) uniform sampler2D gbufferDiffuse;
    // layout(set = n, binding = 1) uniform sampler2D gbufferSpecular;
    // layout(set = n, binding = 2) uniform sampler2D gbufferNormal;
    // layout(set = n, binding = 3) uniform sampler2D gbufferWorldPosition;
    // layout(set = n, binding = 4) uniform sampler2D
    // gbufferOcclusionRoughnessMetallic;
    static auto allocateDescriptorSetLayout(VkDevice)
        -> std::optional<VkDescriptorSetLayout>;
    auto descriptor() const -> VkDescriptorSet;

    [[nodiscard]] auto capacity() const -> std::optional<VkExtent2D>;

    void setSize(VkRect2D);
    [[nodiscard]] auto size() const -> VkRect2D;

    void
    recordTransitionImages(VkCommandBuffer cmd, VkImageLayout dstLayout) const;

    [[nodiscard]] auto attachmentInfo(VkImageLayout) const -> std::array<
        VkRenderingAttachmentInfo,
        static_cast<size_t>(GBufferTextureIndices::COUNT)>;

private:
    VkDevice m_device{VK_NULL_HANDLE};

    VkRect2D m_size{};

    static constexpr size_t GBUFFER_TEXTURE_COUNT{5};

    GBufferTextures m_textures{};

    std::vector<VkSampler> m_immutableSamplers{};

    VkDescriptorSetLayout m_descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptors{VK_NULL_HANDLE};

    std::unique_ptr<DescriptorAllocator> m_descriptorAllocator{};
};

struct GBufferPipeline
{
public:
    auto operator=(GBufferPipeline&&) -> GBufferPipeline&;
    GBufferPipeline(GBufferPipeline&&);

    auto operator=(GBufferPipeline const&) -> GBufferPipeline& = delete;
    GBufferPipeline(GBufferPipeline const&) = delete;

    ~GBufferPipeline();

private:
    GBufferPipeline() = default;

public:
    struct RendererArguments
    {
        VkFormat color;
        VkFormat depth;
        bool reverseZ;
    };

    static auto create(VkDevice, RendererArguments)
        -> std::optional<GBufferPipeline>;

    // Render target is needed for depth and to determine the viewport
    void
    recordDraw(VkCommandBuffer, RenderTarget& renderTarget, GBuffer&, Scene&);

private:
    VkDevice m_device;

    VkPipelineLayout m_graphicsLayout{VK_NULL_HANDLE};

    VkShaderEXT m_vertexStage{VK_NULL_HANDLE};
    VkShaderEXT m_fragmentStage{VK_NULL_HANDLE};

    RendererArguments m_creationArguments{};
};

} // namespace vkt