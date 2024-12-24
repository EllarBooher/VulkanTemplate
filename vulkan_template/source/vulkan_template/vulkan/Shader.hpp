#pragma once

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

auto loadShaderModule(VkDevice, std::filesystem::path const& path)
    -> std::optional<VkShaderModule>;

void computeDispatch(
    VkCommandBuffer, VkExtent3D invocations, VkExtent3D workgroupSize
);
} // namespace vkt