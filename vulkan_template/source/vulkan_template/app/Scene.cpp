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

auto vkt::Scene::cameraOrientation() const -> glm::quat
{
    // Apply Z (roll), then X (pitch), then Y (yaw)
    auto const X{glm::angleAxis(cameraAxisAngles.x, glm::vec3{1.0F, 0.0F, 0.0F})
    };
    auto const Y{glm::angleAxis(cameraAxisAngles.y, glm::vec3{0.0F, 1.0F, 0.0F})
    };
    auto const Z{glm::angleAxis(cameraAxisAngles.z, glm::vec3{0.0F, 0.0F, 1.0F})
    };
    return Z * X * Y;
}

auto vkt::Scene::cameraProjView(float const aspectRatio) const -> glm::mat4x4
{
    float const swappedNear{10'000.0F};
    float const swappedFar{0.1F};

    float const fovRadians{glm::radians(70.0F)};

    // Use LH (opposite of our right handed) since we reverse depth
    glm::mat4x4 const projection{
        glm::perspectiveLH_ZO(fovRadians, aspectRatio, swappedNear, swappedFar)
    };
    glm::mat4x4 const view{glm::inverse(
        glm::translate(cameraPosition) * glm::toMat4(cameraOrientation())
    )};

    return projection * view;
}

void vkt::Scene::controlsWindow(std::optional<ImGuiID> const dockNode)
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
        cameraPosition,
        glm::vec3{0.0F},
        cameraPositionBehavior
    );

    PropertySliderBehavior const axisAnglesBehavior{
        .bounds = FloatBounds{.min = -glm::pi<float>(), .max = glm::pi<float>()}
    };

    table.rowVec3(
        "Camera Orientation", cameraAxisAngles, {}, axisAnglesBehavior
    );

    table.end();
}

auto vkt::Scene::create(
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
