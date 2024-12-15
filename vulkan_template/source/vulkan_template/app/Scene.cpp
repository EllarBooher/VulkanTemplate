#include "Scene.hpp"

#include "vulkan_template/app/PropertyTable.hpp"
#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/UIWindowScope.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <span>
#include <utility>
#include <vector>

namespace vkt
{
// NOLINTBEGIN(readability-magic-numbers)
Camera Scene::DEFAULT_CAMERA{
    .position = glm::vec3{0.0F, 0.0F, -5.0F},
    .axisAngles = glm::vec3{0.0F},
};
// NOLINTEND(readability-magic-numbers)

auto Scene::cameraOrientation() const -> glm::quat
{
    // Apply Z (roll), then X (pitch), then Y (yaw)
    auto const X{
        glm::angleAxis(m_camera.axisAngles.x, glm::vec3{1.0F, 0.0F, 0.0F})
    };
    auto const Y{
        glm::angleAxis(m_camera.axisAngles.y, glm::vec3{0.0F, 1.0F, 0.0F})
    };
    auto const Z{
        glm::angleAxis(m_camera.axisAngles.z, glm::vec3{0.0F, 0.0F, 1.0F})
    };
    return Z * X * Y;
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
        glm::translate(m_camera.position) * glm::toMat4(cameraOrientation())
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

    PropertySliderBehavior const cameraPositionBehavior{.speed = 0.1F};

    PropertyTable table{PropertyTable::begin()};
    table.rowVec3(
        "Camera Position",
        m_camera.position,
        glm::vec3{0.0F},
        cameraPositionBehavior
    );

    PropertySliderBehavior const axisAnglesBehavior{
        .bounds = FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
    };

    table.rowVec3(
        "Camera Orientation", m_camera.axisAngles, {}, axisAnglesBehavior
    );

    table.end();
}

auto Scene::create(
    VkDevice const device,
    VmaAllocator const allocator,
    ImmediateSubmissionQueue& modelUploadQueue
) -> std::optional<Scene>
{
    std::optional<Scene> sceneResult{std::in_place};
    Scene& scene{sceneResult.value()};

    std::vector<glm::mat4x4> const models{glm::identity<glm::mat4x4>()};

    auto const bufferSize{static_cast<VkDeviceSize>(models.size())};

    scene.models = std::make_unique<TStagedBuffer<glm::mat4x4>>(
        TStagedBuffer<glm::mat4x4>::allocate(
            device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, bufferSize
        )
    );
    scene.modelInverseTransposes = std::make_unique<TStagedBuffer<glm::mat4x4>>(
        TStagedBuffer<glm::mat4x4>::allocate(
            device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, bufferSize
        )
    );

    std::span<glm::mat4x4> const modelsMapped{scene.models->mapFullCapacity()};
    std::span<glm::mat4x4> const modelInverseTransposesMapped{
        scene.modelInverseTransposes->mapFullCapacity()
    };
    scene.models->resizeStaged(bufferSize);
    scene.modelInverseTransposes->resizeStaged(bufferSize);

    for (size_t index{0}; index < models.size(); index++)
    {
        modelsMapped[index] = models[index];
        modelInverseTransposesMapped[index] =
            glm::inverseTranspose(models[index]);
    }

    modelUploadQueue.immediateSubmit(
        [&](VkCommandBuffer cmd)
    {
        scene.models->recordCopyToDevice(cmd);
        scene.modelInverseTransposes->recordCopyToDevice(cmd);
    }
    );

    return sceneResult;
}
auto Scene::camera() -> Camera& { return m_camera; }
auto Scene::camera() const -> Camera const& { return m_camera; }
} // namespace vkt