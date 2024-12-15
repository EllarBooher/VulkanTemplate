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
struct Camera
{
    glm::vec3 position;
    glm::vec3 axisAngles;
};

struct Scene
{
    [[nodiscard]] auto cameraOrientation() const -> glm::quat;
    [[nodiscard]] auto cameraProjView(float aspectRatio) const -> glm::mat4x4;

    std::unique_ptr<Mesh> mesh;
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> models{};
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> modelInverseTransposes{};

    void controlsWindow(std::optional<ImGuiID> dockNode);

    static auto
    create(VkDevice, VmaAllocator, ImmediateSubmissionQueue& modelUploadQueue)
        -> std::optional<Scene>;

    [[nodiscard]] auto camera() -> Camera&;
    [[nodiscard]] auto camera() const -> Camera const&;

private:
    static Camera DEFAULT_CAMERA;
    Camera m_camera{DEFAULT_CAMERA};
};
} // namespace vkt