#pragma once

#include "vulkan_template/app/DescriptorAllocator.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/Buffers.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace vkt
{
struct ImmediateSubmissionQueue;
struct ImageView;
} // namespace vkt

namespace vkt
{
struct VertexPacked
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};
// NOLINTNEXTLINE(readability-magic-numbers)
static_assert(sizeof(VertexPacked) == 48ULL);

struct MeshBuffers
{
    static auto uploadMeshData(
        VkDevice,
        VmaAllocator,
        ImmediateSubmissionQueue const&,
        std::span<uint32_t const> indices,
        std::span<VertexPacked const> vertices
    ) -> std::optional<MeshBuffers>;

    auto operator=(MeshBuffers const& other) -> MeshBuffers& = delete;
    MeshBuffers(MeshBuffers const& other) = delete;

    auto operator=(MeshBuffers&& other) -> MeshBuffers& = default;
    MeshBuffers(MeshBuffers&& other) = default;

    // These are not const since they give access to the underlying memory.

    auto indexAddress() -> VkDeviceAddress
    {
        return m_indexBuffer->deviceAddress();
    }
    auto indexBuffer() -> VkBuffer { return m_indexBuffer->buffer(); }

    auto vertexAddress() -> VkDeviceAddress
    {
        return m_vertexBuffer->deviceAddress();
    }
    auto vertexBuffer() -> VkBuffer { return m_vertexBuffer->buffer(); }

private:
    MeshBuffers() = default;

    std::unique_ptr<AllocatedBuffer> m_indexBuffer;
    std::unique_ptr<AllocatedBuffer> m_vertexBuffer;
};

struct MaterialMaps
{
    std::shared_ptr<ImageView> color{};
    std::shared_ptr<ImageView> normal{};
    VkDescriptorSet descriptor{VK_NULL_HANDLE};
};

struct GeometrySurface
{
    uint32_t firstIndex;
    uint32_t indexCount;
    MaterialMaps material;
};

struct MaterialDescriptorPool
{
    auto operator=(MaterialDescriptorPool const&) = delete;
    MaterialDescriptorPool(MaterialDescriptorPool const&) = delete;

    auto operator=(MaterialDescriptorPool&&) noexcept;
    MaterialDescriptorPool(MaterialDescriptorPool&&) noexcept;

    static auto create(VkDevice) -> std::optional<MaterialDescriptorPool>;
    void fillMaterial(MaterialMaps&);

    static auto allocateMaterialDescriptorLayout(VkDevice)
        -> std::optional<VkDescriptorSetLayout>;

    ~MaterialDescriptorPool();

private:
    MaterialDescriptorPool() = default;

    VkDevice device{VK_NULL_HANDLE};

    // All material maps use the same sampler.
    // CANNOT use immutable sampler, since the layout above is statically
    // allocated.
    // Using an immutable sampler would mean the layouts are not identically
    // defined with pipelines and thus the set will not be compatible.
    VkSampler materialSampler{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialLayout{VK_NULL_HANDLE};
    std::unique_ptr<DescriptorAllocator> materialAllocator{};
};

struct Mesh
{
    static auto fromPath(
        VkDevice,
        VmaAllocator,
        ImmediateSubmissionQueue const&,
        MaterialDescriptorPool&,
        std::filesystem::path const& path
    ) -> std::vector<Mesh>;

    std::vector<GeometrySurface> surfaces{};
    std::unique_ptr<MeshBuffers> meshBuffers{};
};
} // namespace vkt