#include "Mesh.hpp"

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <cassert>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp> // IWYU pragma: keep
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <spdlog/fmt/bundled/core.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace
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
} // namespace

namespace
{
namespace detail_stbi
{
auto loadRGBA(std::span<uint8_t const> const bytes)
    -> std::optional<vkt::ImageRGBA>
{
    int32_t x{0};
    int32_t y{0};

    int32_t components{0};
    uint16_t constexpr RGBA_COMPONENT_COUNT{4};

    stbi_uc* const parsedImage{stbi_load_from_memory(
        bytes.data(),
        static_cast<int32_t>(bytes.size()),
        &x,
        &y,
        &components,
        RGBA_COMPONENT_COUNT
    )};

    if (parsedImage == nullptr)
    {
        VKT_ERROR("stbi: Failed to convert image.");
        return std::nullopt;
    }

    if (x < 1 || y < 1)
    {
        VKT_ERROR(fmt::format(
            "stbi: Parsed image had invalid dimensions: ({},{})", x, y
        ));
        return std::nullopt;
    }

    auto widthPixels{static_cast<uint32_t>(x)};
    auto heightPixels{static_cast<uint32_t>(y)};
    size_t constexpr BYTES_PER_COMPONENT{1};
    size_t constexpr BYTES_PER_PIXEL{
        RGBA_COMPONENT_COUNT * BYTES_PER_COMPONENT
    };
    std::vector<uint8_t> const rgba{
        parsedImage,
        parsedImage
            + static_cast<size_t>(widthPixels * heightPixels) * BYTES_PER_PIXEL
    };

    stbi_image_free(parsedImage);

    return vkt::ImageRGBA{.x = widthPixels, .y = heightPixels, .bytes = rgba};
}
} // namespace detail_stbi

namespace detail_fastgltf
{
// glTF material texture indices organized into our engine's format
struct MaterialTextureIndices
{
    std::optional<size_t> color{};
    std::optional<size_t> normal{};
};

enum class MapTypes
{
    Color,
    Normal
};
auto string_MapTypes(MapTypes const mapType)
{
    switch (mapType)
    {
    case MapTypes::Color:
        return "color";
    case MapTypes::Normal:
        return "normal";
    }
}

// Preserves glTF indexing.
auto getTextureSources(
    std::span<fastgltf::Texture const> const textures,
    std::span<fastgltf::Image const> const images
) -> std::vector<std::optional<std::reference_wrapper<fastgltf::Image const>>>
{
    std::vector<std::optional<std::reference_wrapper<fastgltf::Image const>>>
        textureSourcesByGLTFIndex{};
    textureSourcesByGLTFIndex.reserve(textures.size());
    for (fastgltf::Texture const& texture : textures)
    {
        textureSourcesByGLTFIndex.emplace_back(std::nullopt);
        auto& sourceImage{textureSourcesByGLTFIndex.back()};

        if (!texture.imageIndex.has_value())
        {
            VKT_WARNING("Texture {} was missing imageIndex.", texture.name);
            continue;
        }

        size_t const loadedIndex{texture.imageIndex.value()};

        if (loadedIndex >= textures.size())
        {
            VKT_WARNING(
                "Texture {} had imageIndex that was out of bounds.",
                texture.name
            );
            continue;
        }

        sourceImage = images[loadedIndex];
    }

    return textureSourcesByGLTFIndex;
}

auto convertGLTFImageToRGBA(
    fastgltf::DataSource const& source,
    std::filesystem::path const& assetRoot,
    std::optional<std::reference_wrapper<fastgltf::BufferView const>> const
        view,
    std::span<fastgltf::Buffer const> const buffers,
    std::span<fastgltf::BufferView const> const bufferViews
) -> std::optional<vkt::ImageRGBA>
{
    std::optional<vkt::ImageRGBA> result{std::nullopt};

    if (view.has_value())
    {
        assert(view.value().get().byteLength > 0);
    }

    if (std::holds_alternative<fastgltf::sources::Array>(source))
    {
        std::span<uint8_t const> data{
            std::get<fastgltf::sources::Array>(source).bytes
        };
        if (view.has_value()
            && view.value().get().byteLength + view.value().get().byteOffset
                   > data.size())
        {
            VKT_ERROR("Not enough bytes in glTF source for specified view.");
            return std::nullopt;
        }
        if (view.has_value())
        {
            data = {
                data.data() + view.value().get().byteOffset,
                view.value().get().byteLength
            };
        }

        std::optional<vkt::ImageRGBA> imageConvertResult{
            detail_stbi::loadRGBA(data)
        };

        if (imageConvertResult.has_value())
        {
            result = std::move(imageConvertResult.value());
        }
    }
    else if (std::holds_alternative<fastgltf::sources::URI>(source))
    {
        fastgltf::sources::URI const& uri{
            std::get<fastgltf::sources::URI>(source)
        };

        // These asserts should be loosened as we support a larger subset of
        // glTF.
        assert(uri.fileByteOffset == 0);
        assert(uri.uri.isLocalPath());

        std::filesystem::path const path{assetRoot / uri.uri.fspath()};

        std::ifstream file(path, std::ios::binary);

        if (!std::filesystem::is_regular_file(path))
        {
            VKT_WARNING(
                "glTF image source URI does not result in a valid file path. "
                "URI was: {}. Full path is: {}",
                uri.uri.string(),
                path.string()
            );
            return std::nullopt;
        }

        size_t const fileSize{std::filesystem::file_size(path)};

        size_t byteOffset{0};
        size_t byteLength{fileSize};

        if (view.has_value())
        {
            byteOffset = view.value().get().byteOffset;
            byteLength = view.value().get().byteLength;
        }

        if (byteOffset + byteLength > fileSize)
        {
            VKT_ERROR("Not enough bytes in file sourced from glTF URI.");
            return std::nullopt;
        }

        std::vector<uint8_t> data(byteLength);
        file.ignore(static_cast<std::streamsize>(byteOffset));
        file.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(byteLength)
        );

        // Throw the file to stbi and hope for the best, it should detect the
        // file headers properly

        std::optional<vkt::ImageRGBA> imageConvertResult{
            detail_stbi::loadRGBA(data)
        };

        if (imageConvertResult.has_value())
        {
            result = std::move(imageConvertResult.value());
        }
    }
    else if (std::holds_alternative<fastgltf::sources::BufferView>(source))
    {
        // buffer views cannot view buffer views
        assert(!view.has_value());

        size_t const bufferViewIndex =
            std::get<fastgltf::sources::BufferView>(source).bufferViewIndex;

        fastgltf::BufferView const& bufferView{bufferViews[bufferViewIndex]};

        assert(!bufferView.byteStride.has_value());
        assert(bufferView.meshoptCompression == nullptr);
        assert(!bufferView.target.has_value());

        size_t const bufferIndex{bufferView.bufferIndex};

        fastgltf::Buffer const& buffer{buffers[bufferIndex]};

        return convertGLTFImageToRGBA(
            buffer.data, assetRoot, bufferView, buffers, bufferViews
        );
    }
    else
    {
        VKT_WARNING("Unsupported glTF image source found.");
        return std::nullopt;
    }

    if (!result.has_value())
    {
        VKT_WARNING("Failed to load image from glTF.");
        return std::nullopt;
    }

    return result;
}

// Preserves gltf indexing. Returns a vector whose size matches the count of
// materials passed in.
auto parseMaterialIndices(fastgltf::Material const& material)
    -> MaterialTextureIndices
{
    MaterialTextureIndices indices{};

    {
        std::optional<fastgltf::TextureInfo> const& color{
            material.pbrData.baseColorTexture
        };
        if (!color.has_value())
        {
            VKT_WARNING("Material {}: Missing color texture.", material.name);
        }
        else
        {
            indices.color = color.value().textureIndex;
        }
    }

    {
        std::optional<fastgltf::NormalTextureInfo> const& normal{
            material.normalTexture
        };
        if (!normal.has_value())
        {
            VKT_WARNING("Material {}: Missing normal texture.", material.name);
        }
        else
        {
            indices.normal = normal.value().textureIndex;
        }
    }

    return indices;
}

auto accessTexture(
    std::span<std::optional<std::reference_wrapper<fastgltf::Image const>>>
        textureSourcesByGLTFIndex,
    size_t const textureIndex
) -> std::optional<std::reference_wrapper<fastgltf::Image const>>
{
    if (textureIndex >= textureSourcesByGLTFIndex.size())
    {
        VKT_WARNING("Out of bounds texture index.");
        return std::nullopt;
    }

    if (!textureSourcesByGLTFIndex[textureIndex].has_value())
    {
        VKT_WARNING("Texture index source was not loaded.");
        return std::nullopt;
    }

    return textureSourcesByGLTFIndex[textureIndex].value().get();
}

auto uploadTextureFromIndex(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue,
    std::span<std::optional<std::reference_wrapper<fastgltf::Image const>>>
        textureSourcesByGLTFIndex,
    size_t const textureIndex,
    std::filesystem::path const& assetRoot,
    std::span<fastgltf::Buffer const> const buffers,
    std::span<fastgltf::BufferView const> const bufferViews,
    MapTypes const mapType
) -> std::optional<vkt::ImageView>
{
    std::optional<std::reference_wrapper<fastgltf::Image const>> textureResult{
        accessTexture(textureSourcesByGLTFIndex, textureIndex)
    };
    if (!textureResult.has_value())
    {
        return std::nullopt;
    }

    std::optional<vkt::ImageRGBA> convertResult{convertGLTFImageToRGBA(
        textureResult.value().get().data,
        assetRoot,
        std::nullopt,
        buffers,
        bufferViews
    )};
    if (!convertResult.has_value())
    {
        return std::nullopt;
    }

    VkFormat fileFormat{};
    switch (mapType)
    {
    case MapTypes::Color:
        fileFormat = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case MapTypes::Normal:
        fileFormat = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    }

    return vkt::ImageView::uploadToDevice(
        device,
        allocator,
        submissionQueue,
        fileFormat,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        convertResult.value()
    );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto uploadMaterialDataAsAssets(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue,
    vkt::MaterialMaps const& fallbackMaterialData,
    fastgltf::Asset const& gltf,
    std::filesystem::path const& assetRoot
) -> std::vector<vkt::MaterialMaps>
{
    // Follow texture.imageIndex -> image indirection by one step
    std::vector<std::optional<std::reference_wrapper<fastgltf::Image const>>>
        textureSourcesByGLTFIndex{
            detail_fastgltf::getTextureSources(gltf.textures, gltf.images)
        };

    std::vector<vkt::MaterialMaps> materialDataByGLTFIndex{};
    materialDataByGLTFIndex.reserve(gltf.materials.size());
    for (fastgltf::Material const& material : gltf.materials)
    {
        materialDataByGLTFIndex.push_back(fallbackMaterialData);
        vkt::MaterialMaps& materialMaps{materialDataByGLTFIndex.back()};

        detail_fastgltf::MaterialTextureIndices materialTextures{
            parseMaterialIndices(material)
        };

        if (materialTextures.color.has_value())
        {
            if (std::optional<vkt::ImageView> textureLoadResult{
                    uploadTextureFromIndex(
                        device,
                        allocator,
                        submissionQueue,
                        textureSourcesByGLTFIndex,
                        materialTextures.color.value(),
                        assetRoot,
                        gltf.buffers,
                        gltf.bufferViews,
                        MapTypes::Color
                    )
                };
                !textureLoadResult.has_value())
            {
                VKT_WARNING(
                    "Material {}: Failed to upload color texture.",
                    material.name
                );
            }
            else
            {
                materialMaps.color = std::make_unique<vkt::ImageView>(
                    std::move(textureLoadResult).value()
                );
            }
        }

        if (materialTextures.normal.has_value())
        {
            if (std::optional<vkt::ImageView> textureLoadResult{
                    uploadTextureFromIndex(
                        device,
                        allocator,
                        submissionQueue,
                        textureSourcesByGLTFIndex,
                        materialTextures.normal.value(),
                        assetRoot,
                        gltf.buffers,
                        gltf.bufferViews,
                        MapTypes::Normal
                    )
                };
                !textureLoadResult.has_value())
            {
                VKT_WARNING(
                    "Material {}: Failed to upload normal texture.",
                    material.name
                );
            }
            else
            {
                materialMaps.normal = std::make_unique<vkt::ImageView>(
                    std::move(textureLoadResult).value()
                );
            }
        }
    }

    return materialDataByGLTFIndex;
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
    vkt::MaterialMaps const& defaultMaterialData,
    fastgltf::Asset const& gltf,
    std::filesystem::path const& assetRoot
) -> std::vector<vkt::Mesh>
{
    std::vector<vkt::MaterialMaps> materialByGLTFIndex{
        detail_fastgltf::uploadMaterialDataAsAssets(
            device,
            allocator,
            submissionQueue,
            defaultMaterialData,
            gltf,
            assetRoot
        )
    };

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
            else if (size_t const materialIndex{primitive.materialIndex.value()
                     };
                     materialIndex >= materialByGLTFIndex.size())
            {
                VKT_WARNING(
                    "Mesh {} has a primitive with out of bounds material "
                    "index.",
                    mesh.name
                );
            }
            else
            {
                surface.material = materialByGLTFIndex[materialIndex];
            }

            if (!primitive.materialIndex.has_value())
            {
                VKT_WARNING(
                    "Mesh {} has a primitive that is missing material "
                    "index.",
                    mesh.name
                );

                surface.material = defaultMaterialData;
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

        bool constexpr CONVERT_FROM_GLTF_COORDS{true};

        // We use glTF/Vulkan standard CCW front-face winding
        bool constexpr REVERSE_WINDING{false};

        if (CONVERT_FROM_GLTF_COORDS)
        {
            // glTF: +Y up, +Z forward, -X right
            // us: -Y up, +Z forward, +X right
            // Both right handed, but we need to rotate by flipping Y and X

            for (vkt::VertexPacked& vertex : vertices)
            {
                vertex.normal.y *= -1;
                vertex.normal.x *= -1;
                vertex.position.y *= -1;
                vertex.position.x *= -1;
            }
        }
        if (REVERSE_WINDING)
        {
            assert(indices.size() % 3 == 0);
            for (size_t triIndex{0}; triIndex < indices.size() / 3; triIndex++)
            {
                // Engine uses left-handed winding, while glTF uses right-handed
                // We just flipped two axes, so we need to flip the triangle
                // winding too
                std::swap(indices[triIndex * 3 + 1], indices[triIndex * 3 + 2]);
            }
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

        std::vector<vkt::DescriptorAllocator::PoolSizeRatio> const
            materialPoolRatios{
                {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .ratio = 1.0F}
            };
        newMesh.materialAllocator = std::make_unique<vkt::DescriptorAllocator>(
            vkt::DescriptorAllocator::create(
                device, newMesh.surfaces.size(), materialPoolRatios, (VkFlags)0
            )
        );
        newMesh.materialLayout =
            vkt::Mesh::allocateMaterialDescriptorLayout(device).value();

        VkSamplerCreateInfo const samplerInfo{vkt::samplerCreateInfo(
            static_cast<VkFlags>(0),
            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            VK_FILTER_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_REPEAT
        )};

        // Assert since failing would mean leaking a descriptor set
        VKT_CHECK_VK(vkCreateSampler(
            device, &samplerInfo, nullptr, &newMesh.materialSampler
        ));

        for (auto& surface : newMesh.surfaces)
        {
            surface.material.descriptor = newMesh.materialAllocator->allocate(
                device, newMesh.materialLayout
            );

            std::vector<VkDescriptorImageInfo> bindings{
                VkDescriptorImageInfo{
                    .sampler = newMesh.materialSampler,
                    .imageView = surface.material.color->view(),
                    .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                },
                VkDescriptorImageInfo{
                    .sampler = newMesh.materialSampler,
                    .imageView = surface.material.normal->view(),
                    .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                }
            };
            VkWriteDescriptorSet const write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,

                .dstSet = surface.material.descriptor,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = static_cast<uint32_t>(bindings.size()),
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,

                .pImageInfo = bindings.data(),
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            };

            std::vector<VkWriteDescriptorSet> writes{write};

            vkUpdateDescriptorSets(device, VKR_ARRAY(writes), VKR_ARRAY_NONE);
        }

        newMeshes.push_back(std::move(newMesh));
    }

    return newMeshes;
}
} // namespace detail_fastgltf
} // namespace

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

    MaterialMaps defaultMaterial{};

    size_t constexpr DEFAULT_IMAGE_DIMENSIONS{64ULL};

    ImageRGBA defaultImage{
        .x = DEFAULT_IMAGE_DIMENSIONS,
        .y = DEFAULT_IMAGE_DIMENSIONS,
        .bytes = std::vector<uint8_t>{}
    };
    defaultImage.bytes.resize(
        static_cast<size_t>(defaultImage.x)
        * static_cast<size_t>(defaultImage.y) * sizeof(RGBATexel)
    );

    {
        // Default color texture is a grey checkerboard

        size_t index{0};
        for (RGBATexel& texel : std::span<RGBATexel>{
                 reinterpret_cast<RGBATexel*>(defaultImage.bytes.data()),
                 defaultImage.bytes.size() / sizeof(RGBATexel)
             })
        {
            size_t const x{index % defaultImage.x};
            size_t const y{index / defaultImage.x};

            RGBATexel constexpr LIGHT_GREY{
                .r = 200U, .g = 200U, .b = 200U, .a = 255U
            };
            RGBATexel constexpr DARK_GREY{
                .r = 100U, .g = 100U, .b = 100U, .a = 255U
            };

            bool const lightSquare{((x / 4) + (y / 4)) % 2 == 0};

            texel = lightSquare ? LIGHT_GREY : DARK_GREY;

            index++;
        }

        defaultMaterial.color = std::make_shared<vkt::ImageView>(
            vkt::ImageView::uploadToDevice(
                device,
                allocator,
                submissionQueue,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                defaultImage
            )
                .value()
        );
    }
    {
        // Default normal texture

        for (RGBATexel& texel : std::span<RGBATexel>{
                 reinterpret_cast<RGBATexel*>(defaultImage.bytes.data()),
                 defaultImage.bytes.size() / sizeof(RGBATexel)
             })
        {
            // Signed normal of (0,0,1) stored as unsigned (0.5,0.5,1.0)
            RGBATexel constexpr DEFAULT_NORMAL{
                .r = 127U, .g = 127U, .b = 255U, .a = 0U
            };

            texel = DEFAULT_NORMAL;
        }

        defaultMaterial.normal = std::make_shared<vkt::ImageView>(
            vkt::ImageView::uploadToDevice(
                device,
                allocator,
                submissionQueue,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                defaultImage
            )
                .value()
        );
    }

    std::vector<Mesh> newMeshes{detail_fastgltf::loadMeshes(
        device,
        allocator,
        submissionQueue,
        defaultMaterial,
        gltf,
        path.parent_path()
    )};

    VKT_INFO("Loaded {} meshes from glTF", newMeshes.size());

    return newMeshes;
}
auto Mesh::allocateMaterialDescriptorLayout(VkDevice const device)
    -> std::optional<VkDescriptorSetLayout>
{
    // Color & normal
    auto const layoutResult{
        DescriptorLayoutBuilder{}
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stageMask =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .bindingFlags = 0,
            })
            .pushBinding(DescriptorLayoutBuilder::BindingParams{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stageMask =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .bindingFlags = 0,
            })
            .build(device, 0)
    };

    if (!layoutResult.has_value())
    {
        VKT_ERROR("Failed to allocate material descriptor layout.");
        return std::nullopt;
    }

    return layoutResult.value();
}
} // namespace vkt