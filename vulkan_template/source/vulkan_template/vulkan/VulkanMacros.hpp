#pragma once

#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <spdlog/fmt/bundled/core.h>
#include <vulkan/vk_enum_string_helper.h>

#define VKR_ARRAY(x) static_cast<uint32_t>((x).size()), (x).data()
#define VKR_ARRAY_NONE 0, nullptr

// TODO: Support zero variadic arguments
#define VKT_LOG_VK(result_expr, ...)                                           \
    if (VkResult const& VKT_LOG_result{result_expr};                           \
        VKT_LOG_result != VK_SUCCESS)                                          \
    {                                                                          \
        VKT_ERROR(                                                             \
            "VkError {} detected: {}",                                         \
            string_VkResult(VKT_LOG_result),                                   \
            fmt::format(__VA_ARGS__)                                           \
        )                                                                      \
    }

#define VKT_LOG_VKB(result_expr, ...)                                          \
    if (auto const& VKT_VKB_LOG_result{result_expr};                           \
        !VKT_VKB_LOG_result.has_value())                                       \
    {                                                                          \
        vkb::Error const error{VKT_VKB_LOG_result.error()};                    \
        VKT_ERROR(                                                             \
            "vkb::Error ({},{}) detected: {}",                                 \
            string_VkResult(error.vk_result),                                  \
            error.type.message(),                                              \
            fmt::format(__VA_ARGS__)                                           \
        )                                                                      \
    }

#define VKT_CHECK_VK(result_expr)                                              \
    {                                                                          \
        if (VkResult const& VKT_CHECK_result{result_expr};                     \
            VKT_CHECK_result != VK_SUCCESS)                                    \
        {                                                                      \
            VKT_ERROR(                                                         \
                "VkError {} detected.", string_VkResult(VKT_CHECK_result)      \
            )                                                                  \
            assert(VKT_CHECK_result == VK_SUCCESS);                            \
        }                                                                      \
    }

// Thin error propagation, logs any result that isn't VK_SUCCESS
#define VKT_TRY_VK(result_expr, message, return_expr)                          \
    if (VkResult const VKT_result{result_expr}; VKT_result != VK_SUCCESS)      \
    {                                                                          \
        VKT_LOG_VK(VKT_result, message);                                       \
        return return_expr;                                                    \
    }

// Thin error propagation, logs any result that isn't VK_SUCCESS and propagates
// it
#define VKT_PROPAGATE_VK(result_expr, message)                                 \
    if (VkResult const VKT_result{result_expr}; VKT_result != VK_SUCCESS)      \
    {                                                                          \
        VKT_LOG_VK(VKT_result, message);                                       \
        return VKT_result;                                                     \
    }