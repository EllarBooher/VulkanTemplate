#pragma once

#include "vulkan_template/vulkan/VulkanUsage.hpp"

inline auto operator==(VkExtent2D const& lhs, VkExtent2D const& rhs) -> bool
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

inline auto operator==(VkOffset2D const& lhs, VkOffset2D const& rhs) -> bool
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline auto operator==(VkRect2D const& lhs, VkRect2D const& rhs) -> bool
{
    return lhs.extent == rhs.extent && lhs.offset == rhs.offset;
}