#include "Mesh.hpp"

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include <cassert>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp> // IWYU pragma: keep
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <functional>
#include <glm/vec2.hpp>
#include <spdlog/fmt/bundled/core.h>
#include <string>
#include <utility>
#include <vector>

namespace detail_fastgltf
{
auto ensureAbsolutePath(
    std::filesystem::path const& path,
    std::filesystem::path const& root = std::filesystem::current_path()
) -> std::filesystem::path
{
    if (path.is_absolute())
    {
        return path;
    }

    return root / path;
}

auto loadGLTFAsset(std::filesystem::path const& path)
    -> fastgltf::Expected<fastgltf::Asset>
{
    std::filesystem::path const assetPath{ensureAbsolutePath(path)};

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(assetPath);

    auto constexpr GLTF_OPTIONS{
        fastgltf::Options::LoadGLBBuffers
        | fastgltf::Options::LoadExternalBuffers
        // | fastgltf::Options::DecomposeNodeMatrices
        //        | fastgltf::Options::LoadExternalImages // Defer loading
        //        images so we have access to the URIs
    };

    fastgltf::Parser parser{};

    if (assetPath.extension() == ".gltf")
    {
        return parser.loadGltfJson(
            &data, assetPath.parent_path(), GLTF_OPTIONS
        );
    }

    return parser.loadGltfBinary(&data, assetPath.parent_path(), GLTF_OPTIONS);
}

// Preserves gltf indexing, with nullptr on any positions where loading
// failed. All passed gltf objects should come from the same object, so
// accessors are utilized properly.
// TODO: simplify and breakup. There are some roadblocks because e.g. fastgltf
// accessors are separate from the mesh
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto loadMeshes(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue,
    fastgltf::Asset const& gltf
) -> std::vector<vkt::Mesh>
{
    std::vector<vkt::Mesh> newMeshes{};
    newMeshes.reserve(gltf.meshes.size());
    for (fastgltf::Mesh const& mesh : gltf.meshes)
    {
        std::vector<uint32_t> indices{};
        std::vector<vkt::VertexPacked> vertices{};

        vkt::Mesh newMesh{};
        std::vector<vkt::GeometrySurface>& surfaces{newMesh.surfaces};

        // Proliferate indices and vertices
        for (auto&& primitive : mesh.primitives)
        {
            if (!primitive.indicesAccessor.has_value()
                || primitive.indicesAccessor.value() >= gltf.accessors.size())
            {
                VKT_WARNING("glTF mesh primitive had no valid indices "
                            "accessor. It will be skipped.");
                continue;
            }
            if (auto const* positionAttribute{primitive.findAttribute("POSITION"
                )};
                positionAttribute == primitive.attributes.end()
                || positionAttribute == nullptr)
            {
                VKT_WARNING("glTF mesh primitive had no valid vertices "
                            "accessor. It will be skipped.");
                continue;
            }

            if (primitive.type != fastgltf::PrimitiveType::Triangles)
            {
                VKT_WARNING("Loading glTF mesh primitive as Triangles mode "
                            "when it is not.");
            }

            surfaces.push_back(vkt::GeometrySurface{
                .firstIndex = static_cast<uint32_t>(indices.size()),
                .indexCount = 0,
            });
            vkt::GeometrySurface& surface{surfaces.back()};

            if (!primitive.materialIndex.has_value())
            {
                VKT_WARNING(
                    "Mesh {} has a primitive that is missing material "
                    "index.",
                    mesh.name
                );
            }

            size_t const initialVertexIndex{vertices.size()};

            { // Indices, not optional
                fastgltf::Accessor const& indicesAccessor{
                    gltf.accessors[primitive.indicesAccessor.value()]
                };

                surface.indexCount =
                    static_cast<uint32_t>(indicesAccessor.count);

                indices.reserve(indices.size() + indicesAccessor.count);
                fastgltf::iterateAccessor<uint32_t>(
                    gltf,
                    indicesAccessor,
                    [&](uint32_t index)
                { indices.push_back(index + initialVertexIndex); }
                );
            }

            { // Positions, not optional
                fastgltf::Accessor const& positionAccessor{
                    gltf.accessors[primitive.findAttribute("POSITION")->second]
                };

                vertices.reserve(vertices.size() + positionAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf,
                    positionAccessor,
                    [&](glm::vec3 position, size_t /*index*/)
                {
                    vertices.push_back(vkt::VertexPacked{
                        .position = position,
                        .uv_x = 0.0F,
                        .normal = glm::vec3{1, 0, 0},
                        .uv_y = 0.0F,
                        .color = glm::vec4{1.0F},
                    });
                }
                );
            }

            // The rest of these parameters are optional.

            { // Normals
                auto const* const normals{primitive.findAttribute("NORMAL")};
                if (normals != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        gltf,
                        gltf.accessors[(*normals).second],
                        [&](glm::vec3 normal, size_t index)
                    { vertices[initialVertexIndex + index].normal = normal; }
                    );
                }
            }

            { // UVs
                auto const* const uvs{primitive.findAttribute("TEXCOORD_0")};
                if (uvs != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(
                        gltf,
                        gltf.accessors[(*uvs).second],
                        [&](glm::vec2 texcoord, size_t index)
                    {
                        vertices[initialVertexIndex + index].uv_x = texcoord.x;
                        vertices[initialVertexIndex + index].uv_y = texcoord.y;
                    }
                    );
                }
            }

            { // Colors
                auto const* const colors{primitive.findAttribute("COLOR_0")};
                if (colors != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        gltf,
                        gltf.accessors[(*colors).second],
                        [&](glm::vec4 color, size_t index)
                    { vertices[initialVertexIndex + index].color = color; }
                    );
                }
            }
        }

        if (surfaces.empty())
        {
            continue;
        }
        if (auto uploadResult{vkt::MeshBuffers::uploadMeshData(
                device, allocator, submissionQueue, indices, vertices
            )};
            uploadResult.has_value())
        {
            newMesh.meshBuffers = std::make_unique<vkt::MeshBuffers>(
                std::move(uploadResult).value()
            );
        }
        else
        {
            VKT_ERROR("Failed to upload vertices/indices.");
            continue;
        }

        bool constexpr CONVERT_FROM_GLTF_COORDS{true};

        if (CONVERT_FROM_GLTF_COORDS)
        {
            for (vkt::VertexPacked& vertex : vertices)
            {
                // Flip y to point y axis up, and flip x to preserve handedness
                vertex.normal.x *= -1;
                vertex.normal.y *= -1;
                vertex.position.x *= -1;
                vertex.position.y *= -1;
            }
            assert(indices.size() % 3 == 0);
            for (size_t triIndex{0}; triIndex < indices.size() / 3; triIndex++)
            {
                // Engine uses left-handed winding, while glTF uses right-handed
                // We just flipped two axes, so we need to flip the triangle
                // winding too
                std::swap(indices[triIndex * 3 + 1], indices[triIndex * 3 + 2]);
            }
        }

        newMeshes.push_back(std::move(newMesh));
    }

    return newMeshes;
}
} // namespace detail_fastgltf

namespace vkt
{
auto MeshBuffers::uploadMeshData(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue const& submissionQueue,
    std::span<uint32_t const> const indices,
    std::span<VertexPacked const> const vertices
) -> std::optional<MeshBuffers>
{
    std::optional<MeshBuffers> result{std::in_place, MeshBuffers{}};
    MeshBuffers& buffers{result.value()};

    size_t const indexBufferSize{indices.size_bytes()};
    size_t const vertexBufferSize{vertices.size_bytes()};

    buffers.m_indexBuffer =
        std::make_unique<AllocatedBuffer>(AllocatedBuffer::allocate(
            device,
            allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        ));

    buffers.m_vertexBuffer =
        std::make_unique<AllocatedBuffer>(AllocatedBuffer::allocate(
            device,
            allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        ));

    AllocatedBuffer stagingBuffer{AllocatedBuffer::allocate(
        device,
        allocator,
        vertexBufferSize + indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY,
        VMA_ALLOCATION_CREATE_MAPPED_BIT
    )};

    assert(
        stagingBuffer.isMapped()
        && "Staging buffer for mesh upload was not mapped."
    );

    stagingBuffer.writeBytes(
        0,
        std::span<uint8_t const>{
            reinterpret_cast<uint8_t const*>(vertices.data()), vertexBufferSize
        }
    );
    stagingBuffer.writeBytes(
        vertexBufferSize,
        std::span<uint8_t const>{
            reinterpret_cast<uint8_t const*>(indices.data()), indexBufferSize
        }
    );

    if (auto result{submissionQueue.immediateSubmit(
            [&](VkCommandBuffer cmd)
    {
        VkBufferCopy const vertexCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = vertexBufferSize,
        };
        vkCmdCopyBuffer(
            cmd, stagingBuffer.buffer(), buffers.vertexBuffer(), 1, &vertexCopy
        );

        VkBufferCopy const indexCopy{
            .srcOffset = vertexBufferSize,
            .dstOffset = 0,
            .size = indexBufferSize,
        };
        vkCmdCopyBuffer(
            cmd, stagingBuffer.buffer(), buffers.indexBuffer(), 1, &indexCopy
        );
    }
        )};
        result != ImmediateSubmissionQueue::SubmissionResult::SUCCESS)
    {
        VKT_ERROR("Vertex/Index buffer submission failed.");
        return std::nullopt;
    }

    return result;
}

auto Mesh::fromPath(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue const& submissionQueue,
    std::filesystem::path const& path
) -> std::vector<Mesh>
{
    VKT_INFO("Loading glTF from {}", path.string());

    fastgltf::Expected<fastgltf::Asset> gltfLoadResult{
        detail_fastgltf::loadGLTFAsset(path)
    };
    if (gltfLoadResult.error() != fastgltf::Error::None)
    {
        VKT_ERROR(fmt::format(
            "Failed to load glTF: {} : {}",
            fastgltf::getErrorName(gltfLoadResult.error()),
            fastgltf::getErrorMessage(gltfLoadResult.error())
        ));
        return {};
    }
    fastgltf::Asset const& gltf{gltfLoadResult.get()};

    std::vector<Mesh> newMeshes{
        detail_fastgltf::loadMeshes(device, allocator, submissionQueue, gltf)
    };

    VKT_INFO("Loaded {} meshes from glTF", newMeshes.size());

    return newMeshes;
}
} // namespace vkt