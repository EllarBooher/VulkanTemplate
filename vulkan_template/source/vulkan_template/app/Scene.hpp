#pragma once

#include "vulkan_template/app/Mesh.hpp"
#include "vulkan_template/vulkan/Buffers.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>
#include <optional>

namespace vkt
{
struct ImmediateSubmissionQueue;
} // namespace vkt

namespace vkt
{
struct Transform
{
    glm::vec3 position;
    glm::vec3 axisAngles;
    glm::vec3 scale;
};

struct InstanceRenderingInfo
{
    VkDeviceSize instanceCount;
    VkDeviceAddress models;
    VkDeviceAddress modelInverseTransposes;
};

struct Scene
{
    [[nodiscard]] auto cameraOrientation() const -> glm::quat;
    [[nodiscard]] auto cameraProjView(float aspectRatio) const -> glm::mat4x4;

    void controlsWindow(std::optional<ImGuiID> dockNode);

    static auto
    create(VkDevice, VmaAllocator, ImmediateSubmissionQueue& modelUploadQueue)
        -> std::optional<Scene>;

    [[nodiscard]] auto camera() -> Transform&;
    [[nodiscard]] auto camera() const -> Transform const&;

    void setMesh(std::unique_ptr<Mesh>);
    [[nodiscard]] auto mesh() -> Mesh&;
    [[nodiscard]] auto mesh() const -> Mesh const&;

    [[nodiscard]] auto instanceRenderingInfo() const -> InstanceRenderingInfo;

    void prepare(VkCommandBuffer);

private:
    static Transform DEFAULT_CAMERA;
    static Transform DEFAULT_MESH_INSTANCE;

    Transform m_camera{DEFAULT_CAMERA};
    Transform m_meshInstance{DEFAULT_MESH_INSTANCE};

    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_models{};
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> m_modelInverseTransposes{};

    std::unique_ptr<Mesh> m_mesh{};
};
} // namespace vkt