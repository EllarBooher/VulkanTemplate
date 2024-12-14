#include "Scene.hpp"

#include "vulkan_template/vulkan/Immediate.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
