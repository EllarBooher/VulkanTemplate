#include "Scene.hpp"

#include "vulkan_template/app/PropertyTable.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/core/UIWindowScope.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp> // IWYU pragma: keep
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <list>
#include <queue>
#include <span>
#include <spdlog/fmt/bundled/core.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vkt
{
struct ImmediateSubmissionQueue;
} // namespace vkt

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

    assert(x > 0 && y > 0);

    auto const width = static_cast<uint32_t>(x);
    auto const height = static_cast<uint32_t>(y);
    std::vector<vkt::RGBATexel> texels{};
    texels.resize(static_cast<size_t>(width) * height);
    std::copy(
        parsedImage,
        parsedImage + texels.size() * sizeof(vkt::RGBATexel),
        reinterpret_cast<uint8_t*>(texels.data())
    );

    std::optional<vkt::ImageRGBA> image{
        std::in_place,
        vkt::ImageRGBA{
            .width = width,
            .height = height,
            .texels = std::move(texels),
        }
    };

    stbi_image_free(parsedImage);

    return image;
}
} // namespace detail_stbi

namespace detail_fastgltf
{
auto loadModelMatricesByGLTFIndex(fastgltf::Asset const& gltf)
    -> std::unordered_map<size_t, std::vector<glm::mat4x4>>
{
    std::unordered_map<size_t, std::vector<glm::mat4x4>>
        modelMatricesByGLTFIndex{};

    if (gltf.scenes.empty())
    {
        return modelMatricesByGLTFIndex;
    }

    // TODO: load all scenes
    fastgltf::Scene const& scene{gltf.scenes[0]};

    struct NodeToProcess
    {
        size_t gltfIndex;
        glm::mat4x4 parentModel;
    };

    std::queue<NodeToProcess> nodes{};
    for (size_t const& nodeIndex : scene.nodeIndices)
    {
        nodes.push(
            {.gltfIndex = nodeIndex, .parentModel = glm::identity<glm::mat4x4>()
            }
        );
    }

    while (!nodes.empty())
    {
        NodeToProcess const node{nodes.front()};
        nodes.pop();

        fastgltf::Node const& gltfNode{gltf.nodes[node.gltfIndex]};

        fastgltf::TRS const trs{std::get<fastgltf::TRS>(gltfNode.transform)};

        glm::quat const orientation{glm::quat::wxyz(
            trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]
        )};

        glm::vec3 const translation{
            trs.translation[0], trs.translation[1], trs.translation[2]
        };

        glm::vec3 const scale{trs.scale[0], trs.scale[1], trs.scale[2]};

        glm::mat4x4 const model{
            node.parentModel * glm::translate(translation)
            * glm::toMat4(orientation) * glm::scale(scale)
        };

        if (gltfNode.meshIndex.has_value())
        {
            glm::mat4x4 const fromGLTF{glm::scale(glm::vec3{-1.0, -1.0, 1.0})};
            glm::mat4x4 const toGLTF{glm::inverse(fromGLTF)};

            glm::mat4x4 const finalModel{fromGLTF * model * toGLTF};

            size_t const meshIndex{gltfNode.meshIndex.value()};
            modelMatricesByGLTFIndex[gltfNode.meshIndex.value()].push_back(
                finalModel
            );
        }

        for (size_t const& gltfIndex : gltfNode.children)
        {
            nodes.push({.gltfIndex = gltfIndex, .parentModel = model});
        }
    }

    return modelMatricesByGLTFIndex;
}

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
        | fastgltf::Options::DecomposeNodeMatrices
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto loadMeshesByGLTFIndex(
    VkDevice const device,
    VmaAllocator const allocator,
    vkt::ImmediateSubmissionQueue const& submissionQueue,
    vkt::MaterialDescriptorPool& materialPool,
    vkt::MaterialMaps const& defaultMaterialData,
    fastgltf::Asset const& gltf,
    std::filesystem::path const& assetRoot
) -> std::unordered_map<size_t, vkt::Mesh>
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

    std::unordered_map<size_t, vkt::Mesh> meshesByGLTFIndex{};
    meshesByGLTFIndex.reserve(gltf.meshes.size());
    for (size_t gltfIndex = 0; gltfIndex < gltf.meshes.size(); gltfIndex++)
    {
        fastgltf::Mesh const& mesh{gltf.meshes[gltfIndex]};

        std::vector<uint32_t> indices{};
        std::vector<vkt::VertexPacked> vertices{};

        vkt::Mesh newMesh{};
        std::vector<vkt::GeometrySurface>& surfaces{newMesh.surfaces};

        // Proliferate indices and vertices
        for (auto&& primitive : mesh.primitives)
        {
            glm::vec4 baseColorFactor{1.0};

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

                surface.material = defaultMaterialData;
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
                baseColorFactor = glm::vec4{
                    gltf.materials[materialIndex].pbrData.baseColorFactor[0],
                    gltf.materials[materialIndex].pbrData.baseColorFactor[1],
                    gltf.materials[materialIndex].pbrData.baseColorFactor[2],
                    gltf.materials[materialIndex].pbrData.baseColorFactor[3]
                };
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
                        .color = baseColorFactor,
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

                surface.hasTexCoords = uvs != primitive.attributes.end();

                if (surface.hasTexCoords)
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
                    { vertices[initialVertexIndex + index].color *= color; }
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
            // https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#coordinate-system-and-units
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

        for (auto& surface : newMesh.surfaces)
        {
            materialPool.fillMaterial(surface.material);
        }

        meshesByGLTFIndex.insert({gltfIndex, std::move(newMesh)});
    }

    return meshesByGLTFIndex;
}
} // namespace detail_fastgltf

namespace
{
auto axisAngleOrientation(glm::vec3 const axisAngle) -> glm::quat
{ // Apply Z (roll), then X (pitch), then Y (yaw)
    auto const X{glm::angleAxis(axisAngle.x, glm::vec3{1.0F, 0.0F, 0.0F})};
    auto const Y{glm::angleAxis(axisAngle.y, glm::vec3{0.0F, 1.0F, 0.0F})};
    auto const Z{glm::angleAxis(axisAngle.z, glm::vec3{0.0F, 0.0F, 1.0F})};
    return Z * X * Y;
}
} // namespace

namespace vkt
{
// NOLINTBEGIN(readability-magic-numbers)
Transform Scene::DEFAULT_CAMERA{
    .translation = glm::vec3{0.0F, 0.0F, -5.0F},
    .axisAngles = glm::vec3{0.0F},
    .scale = glm::vec3{1.0F},
};
// NOLINTEND(readability-magic-numbers)

auto Scene::cameraOrientation() const -> glm::quat
{
    return ::axisAngleOrientation(m_camera.axisAngles);
}

auto Scene::cameraProjView(float const aspectRatio) const -> glm::mat4x4
{
    float const swappedNear{10'000.0F};
    float const swappedFar{0.1F};

    float const fovRadians{glm::radians(70.0F)};

    // Use LH (opposite of our right handed) since we reverse depth
    glm::mat4x4 const projection{
        glm::perspectiveLH_ZO(fovRadians, aspectRatio, swappedNear, swappedFar)
    };
    glm::mat4x4 const view{glm::inverse(
        glm::translate(m_camera.translation) * glm::toMat4(cameraOrientation())
        * glm::scale(m_camera.scale)
    )};

    return projection * view;
}

void Scene::controlsWindow(std::optional<ImGuiID> const dockNode)
{
    char const* const WINDOW_TITLE{"Controls"};

    vkt::UIWindowScope const sceneViewport{
        vkt::UIWindowScope::beginDockable(WINDOW_TITLE, dockNode)
    };

    if (!sceneViewport.isOpen())
    {
        return;
    }

    ImGui::SeparatorText("Camera");

    PropertySliderBehavior constexpr POSITION_BEHAVIOR{.speed = 0.1F};
    PropertySliderBehavior const AXIS_ANGLES_BEHAVIOR{
        .bounds = FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
    };
    PropertySliderBehavior const SCALE_BEHAVIOR{
        .speed = 1.0F,
        .bounds = FloatBounds{.min = 0.0},
    };
    PropertySliderBehavior const SCALE_ALL_BEHAVIOR{
        .speed = 0.1F,
        .bounds = FloatBounds{.min = 0.9, .max = 1.1},
    };

    PropertyTable table{PropertyTable::begin()};
    table.rowVec3(
        {.name = "Camera Position"},
        m_camera.translation,
        DEFAULT_CAMERA.translation,
        POSITION_BEHAVIOR
    );
    table.rowVec3(
        {.name = "Camera Orientation"},
        m_camera.axisAngles,
        DEFAULT_CAMERA.axisAngles,
        AXIS_ANGLES_BEHAVIOR
    );
    table.rowVec3(
        {.name = "Camera Scale"},
        m_camera.scale,
        DEFAULT_CAMERA.scale,
        SCALE_BEHAVIOR
    );

    table.end();
}

auto Scene::camera() -> Transform& { return m_camera; }
auto Scene::camera() const -> Transform const& { return m_camera; }

auto Scene::loadSceneFromDisk(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue const& textureUploadQueue,
    MaterialDescriptorPool& materialPool,
    std::filesystem::path const& path
) -> std::optional<Scene>
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
        .width = DEFAULT_IMAGE_DIMENSIONS,
        .height = DEFAULT_IMAGE_DIMENSIONS,
        .texels = std::vector<RGBATexel>(
            DEFAULT_IMAGE_DIMENSIONS * DEFAULT_IMAGE_DIMENSIONS
        )
    };

    {
        RGBATexel constexpr WHITE{.r = 255U, .g = 255U, .b = 255U, .a = 255U};

        for (RGBATexel& texel : defaultImage.texels)
        {
            texel = WHITE;
        }

        defaultMaterial.color = std::make_shared<vkt::ImageView>(
            vkt::ImageView::uploadToDevice(
                device,
                allocator,
                textureUploadQueue,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                defaultImage
            )
                .value()
        );
    }
    {
        // Default normal texture

        for (RGBATexel& texel : defaultImage.texels)
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
                textureUploadQueue,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                defaultImage
            )
                .value()
        );
    }

    std::unordered_map<size_t, Mesh> meshesByGLTFIndex{
        detail_fastgltf::loadMeshesByGLTFIndex(
            device,
            allocator,
            textureUploadQueue,
            materialPool,
            defaultMaterial,
            gltf,
            path.parent_path()
        )
    };

    VKT_INFO("Loaded {} meshes from glTF", meshesByGLTFIndex.size());

    std::unordered_map<size_t, std::vector<glm::mat4x4>> const
        modelMatricesByGLTFIndex{
            detail_fastgltf::loadModelMatricesByGLTFIndex(gltf)
        };

    std::optional<Scene> sceneResult{std::in_place};
    Scene& scene{sceneResult.value()};

    for (auto const& [gltfIndex, mesh] : meshesByGLTFIndex)
    {
        if (!modelMatricesByGLTFIndex.contains(gltfIndex))
        {
            VKT_INFO(
                "glTF Mesh found with no corresponding loaded model matrices."
            );
            continue;
        }

        auto const& meshMatrices{modelMatricesByGLTFIndex.at(gltfIndex)};

        scene.m_meshes.push_back(std::move(meshesByGLTFIndex[gltfIndex]));
        scene.m_meshModelIndices.push_back(
            {.start = scene.m_meshModels.size(), .count = meshMatrices.size()}
        );
        assert(scene.m_meshes.size() == scene.m_meshModelIndices.size());

        scene.m_meshModels.insert(
            scene.m_meshModels.end(), meshMatrices.begin(), meshMatrices.end()
        );
    }

    scene.m_models = std::make_unique<TStagedBuffer<glm::mat4x4>>(
        TStagedBuffer<glm::mat4x4>::allocate(
            device,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            allocator,
            scene.m_meshModels.size()
        )
    );
    scene.m_modelInverseTransposes =
        std::make_unique<TStagedBuffer<glm::mat4x4>>(
            TStagedBuffer<glm::mat4x4>::allocate(
                device,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                allocator,
                scene.m_meshModels.size()
            )
        );

    return sceneResult;
}

auto Scene::instanceRenderingInfo() const -> InstanceRenderingInfo
{
    assert(
        m_models->deviceSize() == m_modelInverseTransposes->deviceSize()
        && "Model matrices desynced!"
    );

    InstanceRenderingInfo info{
        .instances = {},
        .models = m_models->deviceAddress(),
        .modelInverseTransposes = m_modelInverseTransposes->deviceAddress()
    };

    assert(m_meshes.size() == m_meshModelIndices.size());

    for (size_t meshIndex = 0; meshIndex < m_meshes.size(); meshIndex++)
    {
        info.instances.push_back(InstanceSpan{
            .start = m_meshModelIndices[meshIndex].start,
            .count = m_meshModelIndices[meshIndex].count,
            .mesh = m_meshes[meshIndex]
        });
    }

    return info;
}
void Scene::prepare(VkCommandBuffer const cmd)
{
    std::span<glm::mat4x4> const modelsMapped{m_models->mapFullCapacity()};
    std::span<glm::mat4x4> const modelInverseTransposesMapped{
        m_modelInverseTransposes->mapFullCapacity()
    };

    auto const bufferSize{static_cast<VkDeviceSize>(m_meshModels.size())};

    m_models->resizeStaged(bufferSize);
    m_modelInverseTransposes->resizeStaged(bufferSize);

    for (size_t index{0}; index < m_meshModels.size(); index++)
    {
        modelsMapped[index] = m_meshModels[index];
        modelInverseTransposesMapped[index] =
            glm::inverseTranspose(m_meshModels[index]);
    }

    m_models->recordCopyToDevice(cmd);
    m_modelInverseTransposes->recordCopyToDevice(cmd);

    m_models->recordTotalCopyBarrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
    m_modelInverseTransposes->recordTotalCopyBarrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}
} // namespace vkt