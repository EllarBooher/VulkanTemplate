#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <filesystem>
#include <optional>
#include <span>

namespace vkt
{
auto loadShaderObject(
    VkDevice,
    std::filesystem::path const& path,
    VkShaderStageFlagBits stage,
    VkShaderStageFlags nextStage,
    std::span<VkDescriptorSetLayout const> layouts,
    std::span<VkPushConstantRange const> pushConstantRanges,
    VkSpecializationInfo specializationInfo
) -> std::optional<VkShaderEXT>;

void computeDispatch(
    VkCommandBuffer, VkExtent3D invocations, uint32_t workgroupSize
);
} // namespace vkt