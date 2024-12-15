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
    .position = glm::vec3{0.0F, 0.0F, -5.0F},
    .axisAngles = glm::vec3{0.0F},
};
Transform Scene::DEFAULT_MESH_INSTANCE{};
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

    PropertySliderBehavior constexpr POSITION_BEHAVIOR{.speed = 0.1F};
    PropertySliderBehavior const AXIS_ANGLES_BEHAVIOR{
        .bounds = FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
    };

    PropertyTable table{PropertyTable::begin()};
    table.rowVec3(
        "Camera Position",
        m_camera.position,
        DEFAULT_CAMERA.position,
        POSITION_BEHAVIOR
    );
    table.rowVec3(
        "Camera Orientation",
        m_camera.axisAngles,
        DEFAULT_CAMERA.axisAngles,
        AXIS_ANGLES_BEHAVIOR
    );

    table.rowVec3(
        "Mesh Position",
        m_meshInstance.position,
        DEFAULT_MESH_INSTANCE.position,
        POSITION_BEHAVIOR
    );
    table.rowVec3(
        "Mesh Orientation",
        m_meshInstance.axisAngles,
        DEFAULT_MESH_INSTANCE.axisAngles,
        AXIS_ANGLES_BEHAVIOR
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

    scene.m_models = std::make_unique<TStagedBuffer<glm::mat4x4>>(
        TStagedBuffer<glm::mat4x4>::allocate(
            device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, 1
        )
    );
    scene.m_modelInverseTransposes =
        std::make_unique<TStagedBuffer<glm::mat4x4>>(
            TStagedBuffer<glm::mat4x4>::allocate(
                device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, 1
            )
        );
    return sceneResult;
}
auto Scene::camera() -> Transform& { return m_camera; }
auto Scene::camera() const -> Transform const& { return m_camera; }
void Scene::setMesh(std::unique_ptr<Mesh> mesh) { m_mesh = std::move(mesh); }
auto Scene::mesh() -> Mesh& { return *m_mesh; }
auto Scene::mesh() const -> Mesh const& { return *m_mesh; }
auto Scene::instanceRenderingInfo() const -> InstanceRenderingInfo
{
    assert(
        m_models->deviceSize() == m_modelInverseTransposes->deviceSize()
        && "Model matrices desynced!"
    );
    return {
        .instanceCount = m_models->deviceSize(),
        .models = m_models->deviceAddress(),
        .modelInverseTransposes = m_modelInverseTransposes->deviceAddress()
    };
}
void Scene::prepare(VkCommandBuffer const cmd)
{
    std::vector<glm::mat4x4> const models{
        glm::translate(m_meshInstance.position)
        * glm::toMat4(::axisAngleOrientation(m_meshInstance.axisAngles))
    };

    std::span<glm::mat4x4> const modelsMapped{m_models->mapFullCapacity()};
    std::span<glm::mat4x4> const modelInverseTransposesMapped{
        m_modelInverseTransposes->mapFullCapacity()
    };

    auto const bufferSize{static_cast<VkDeviceSize>(models.size())};

    m_models->resizeStaged(bufferSize);
    m_modelInverseTransposes->resizeStaged(bufferSize);

    for (size_t index{0}; index < models.size(); index++)
    {
        modelsMapped[index] = models[index];
        modelInverseTransposesMapped[index] =
            glm::inverseTranspose(models[index]);
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