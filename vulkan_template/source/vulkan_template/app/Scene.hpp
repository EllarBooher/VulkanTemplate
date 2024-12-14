#pragma once

#include "vulkan_template/app/Mesh.hpp"
#include "vulkan_template/vulkan/Buffers.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <glm/mat4x4.hpp>
#include <memory>

namespace vkt
{
struct ImmediateSubmissionQueue;
} // namespace vkt

namespace vkt
{
struct Scene
{
    std::unique_ptr<Mesh> mesh;
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> models{};
    std::unique_ptr<TStagedBuffer<glm::mat4x4>> modelInverseTransposes{};

    static auto
    create(VkDevice, VmaAllocator, ImmediateSubmissionQueue& modelUploadQueue)
        -> std::optional<Scene>;
};
} // namespace vkt