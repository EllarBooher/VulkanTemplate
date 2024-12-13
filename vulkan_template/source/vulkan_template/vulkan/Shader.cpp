#include "Shader.hpp"

#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace detail
{
auto ensureAbsolutePath(
    std::filesystem::path const& path,
    std::filesystem::path const& root = std::filesystem::current_path()
) -> std::filesystem::path
{
    if (path.is_absolute())
    {
        return path;
    }

    return root / path;
}

auto loadFileBytes(std::filesystem::path const& path) -> std::vector<uint8_t>
{
    std::filesystem::path const assetPath{ensureAbsolutePath(path)};
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        VKT_ERROR("Unable to open file at {}", path.string());
        return {};
    }

    size_t const fileSizeBytes = static_cast<size_t>(file.tellg());
    if (fileSizeBytes == 0)
    {
        VKT_ERROR("File at empty at {}", path.string());
        return {};
    }

    std::vector<uint8_t> buffer(fileSizeBytes);

    file.seekg(0, std::ios::beg);
    file.read(
        reinterpret_cast<char*>(buffer.data()),
        static_cast<std::streamsize>(fileSizeBytes)
    );

    file.close();

    return buffer;
}

auto computeDispatchCount(uint32_t invocations, uint32_t workgroupSize)
    -> uint32_t
{
    // When workgroups are larger than 1, but this value does not evenly divide
    // the amount of work needed, we need to dispatch extra to cover this. It is
    // up to the shader to discard these extra invocations.

    uint32_t const count{invocations / workgroupSize};

    if (invocations % workgroupSize == 0)
    {
        return count;
    }

    return count + 1;
}

} // namespace detail

namespace vkt
{
auto loadShaderObject(
    VkDevice const device,
    std::filesystem::path const& path,
    VkShaderStageFlagBits const stage,
    VkShaderStageFlags const nextStage,
    std::span<VkDescriptorSetLayout const> const layouts,
    std::span<VkPushConstantRange const> const pushConstantRanges,
    VkSpecializationInfo const specializationInfo
) -> std::optional<VkShaderEXT>
{
    std::vector<uint8_t> const fileBytes{detail::loadFileBytes(path)};
    if (fileBytes.empty())
    {
        VKT_ERROR(
            "Failed to load file for Shader Object at '{}'", path.string()
        );
        return std::nullopt;
    }

    VkShaderCreateInfoEXT const createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .pNext = nullptr,

        .flags = 0,

        .stage = stage,
        .nextStage = nextStage,

        .codeType = VkShaderCodeTypeEXT::VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = fileBytes.size(),
        .pCode = fileBytes.data(),

        .pName = "main",

        .setLayoutCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),

        .pushConstantRangeCount =
            static_cast<uint32_t>(pushConstantRanges.size()),
        .pPushConstantRanges = pushConstantRanges.data(),

        .pSpecializationInfo = &specializationInfo,
    };

    VkShaderEXT shaderObject{VK_NULL_HANDLE};
    VkResult const result{
        vkCreateShadersEXT(device, 1, &createInfo, nullptr, &shaderObject)
    };
    if (result != VK_SUCCESS)
    {
        VKT_LOG_VK(result, "Failed to compile Shader Object");
        return std::nullopt;
    }

    return shaderObject;
}
auto loadShaderModule(VkDevice const device, std::filesystem::path const& path)
    -> std::optional<VkShaderModule>
{
    std::vector<uint8_t> const fileBytes{detail::loadFileBytes(path)};
    if (fileBytes.empty())
    {
        VKT_ERROR(
            "Failed to load file for Shader Module at '{}'", path.string()
        );
        return std::nullopt;
    }

    VkShaderModuleCreateInfo const createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .codeSize = fileBytes.size(),
        .pCode = reinterpret_cast<uint32_t const*>(fileBytes.data()),
    };

    VkShaderModule shaderModule{VK_NULL_HANDLE};
    VkResult const result{
        vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule)
    };

    VKT_TRY_VK(result, "Failed to compile Shader Module.", std::nullopt);

    return shaderModule;
}
void computeDispatch(
    VkCommandBuffer const cmd,
    VkExtent3D const invocations,
    uint32_t const workgroupSize
)
{
    uint32_t const X{
        detail::computeDispatchCount(invocations.width, workgroupSize)
    };
    uint32_t const Y{
        detail::computeDispatchCount(invocations.height, workgroupSize)
    };
    uint32_t const Z{
        detail::computeDispatchCount(invocations.depth, workgroupSize)
    };

    vkCmdDispatch(cmd, X, Y, Z);
}
} // namespace vkt