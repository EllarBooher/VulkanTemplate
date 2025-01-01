#pragma once

#include "vulkan_template/app/Mesh.hpp"
#include "vulkan_template/vulkan/Buffers.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <filesystem>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>
#include <optional>
#include <vector>

namespace vkt
{
struct ImmediateSubmissionQueue;
} // namespace vkt

namespace vkt
{
struct Transform
{
    glm::vec3 translation;
    glm::vec3 axisAngles;
    glm::vec3 scale;
};

struct InstanceSpan
{
    VkDeviceSize start;
    VkDeviceSize count;

    std::reference_wrapper<vkt::Mesh const> mesh;
};

struct InstanceRenderingInfo
{
    std::vector<InstanceSpan> instances;
    VkDeviceAddress models;
    VkDeviceAddress modelInverseTransposes;
};

struct Scene
{
    [[nodiscard]] auto cameraOrientation() const -> glm::quat;
    [[nodiscard]] auto cameraProjView(float aspectRatio) const -> glm::mat4x4;

    void controlsWindow(std::optional<ImGuiID> dockNode);

    [[nodiscard]] auto camera() -> Transform&;
    [[nodiscard]] auto camera() const -> Transform const&;

    static auto loadSceneFromDisk(
        VkDevice,
        VmaAllocator,
        ImmediateSubmissionQueue const& textureUploadQueue,
        MaterialDescriptorPool& materialPool,
        std::filesystem::path const& path
    ) -> std::optional<Scene>;

    [[nodiscard]] auto instanceRenderingInfo() const -> InstanceRenderingInfo;

    void prepare(VkCommandBuffer);

private:
    static Transform DEFAULT_CAMERA;

    Transform m_camera{DEFAULT_CAMERA};

    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_models{};
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_modelInverseTransposes{};

    struct MeshModelIndices
    {
        size_t start;
        size_t count;
    };

    // Keep scene data separate from gpu buffers until rendering time
    std::vector<glm::mat4x4> m_meshModels{};
    std::vector<Mesh> m_meshes{};
    std::vector<MeshModelIndices> m_meshModelIndices{};
};
} // namespace vkt