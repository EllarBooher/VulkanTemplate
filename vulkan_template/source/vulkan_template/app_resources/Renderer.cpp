#include "Renderer.hpp"

#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include <filesystem>
#include <fstream>
#include <glm/vec2.hpp>
#include <span>

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

template <typename T> struct ShaderResult
{
    T shader;
    VkResult result;
};

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
    std::vector<uint8_t> const fileBytes{loadFileBytes(path)};
    if (fileBytes.empty())
    {
        VKT_ERROR("Failed to load file for texture at '{}'", path.string());
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
Renderer::Renderer(Renderer&& other) noexcept
{
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_shader = std::exchange(other.m_shader, VK_NULL_HANDLE);
    m_shaderLayout = std::exchange(other.m_shaderLayout, VK_NULL_HANDLE);
    m_destinationSingletonLayout =
        std::exchange(other.m_destinationSingletonLayout, VK_NULL_HANDLE);
}
Renderer::~Renderer()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyShaderEXT(m_device, m_shader, nullptr);
        vkDestroyPipelineLayout(m_device, m_shaderLayout, nullptr);
        vkDestroyDescriptorSetLayout(
            m_device, m_destinationSingletonLayout, nullptr
        );
    }
}
auto Renderer::create(VkDevice const device) -> std::optional<Renderer>
{
    std::optional<Renderer> result{std::in_place, Renderer{}};
    Renderer& renderer{result.value()};
    renderer.m_device = device;

    std::filesystem::path const shaderPath{"shaders/testpattern.comp.spv"};

    if (auto const descriptorResult{SceneTexture::allocateSingletonLayout(device
        )};
        descriptorResult.has_value())
    {
        renderer.m_destinationSingletonLayout = descriptorResult.value();
    }
    else
    {
        VKT_ERROR("Failed to allocator descriptor set layout for scene texture."
        );
        return std::nullopt;
    }

    std::vector<VkDescriptorSetLayout> const layouts{
        renderer.m_destinationSingletonLayout
    };
    std::vector<VkPushConstantRange> const ranges{VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 16
    }};

    std::optional<VkShaderEXT> const shaderResult{detail::loadShaderObject(
        device,
        shaderPath,
        VK_SHADER_STAGE_COMPUTE_BIT,
        (VkFlags)0,
        layouts,
        ranges,
        VkSpecializationInfo{}
    )};
    if (!shaderResult.has_value())
    {
        VKT_ERROR("Failed to compile shaader.");
        return std::nullopt;
    }

    renderer.m_shader = shaderResult.value();

    VkPipelineLayoutCreateInfo const layoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .setLayoutCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),

        .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    };

    if (VkResult const pipelineLayoutResult{vkCreatePipelineLayout(
            device, &layoutCreateInfo, nullptr, &renderer.m_shaderLayout
        )};
        pipelineLayoutResult != VK_SUCCESS)
    {
        VKT_LOG_VK(pipelineLayoutResult, "Failed to create pipeline layout.");
        return std::nullopt;
    }

    return result;
}
void Renderer::recordDraw(VkCommandBuffer const cmd, SceneTexture& destination)
    const
{
    VkShaderStageFlagBits const stage{VK_SHADER_STAGE_COMPUTE_BIT};
    VkPipelineBindPoint const bindPoint{VK_PIPELINE_BIND_POINT_COMPUTE};
    VkShaderEXT const shaderObject{m_shader};
    VkPipelineLayout const layout{m_shaderLayout};

    vkCmdBindShadersEXT(cmd, 1, &stage, &shaderObject);

    // Bind the destination image for rendering during compute
    VkDescriptorSet destinationDescriptor{destination.singletonDescriptor()};
    vkCmdBindDescriptorSets(
        cmd, bindPoint, layout, 0, 1, &destinationDescriptor, VKR_ARRAY_NONE
    );

    struct PushConstant
    {
        glm::vec2 drawOffset{};
        glm::vec2 drawExtent{};
    };

    VkRect2D const drawRect{destination.size()};
    PushConstant const pc{
        .drawOffset =
            glm::vec2{
                static_cast<float>(drawRect.offset.x),
                static_cast<float>(drawRect.offset.y)
            },
        .drawExtent =
            glm::vec2{
                static_cast<float>(drawRect.extent.width),
                static_cast<float>(drawRect.extent.height)
            },
    };

    uint32_t constexpr BYTE_OFFSET{0};
    vkCmdPushConstants(
        cmd, layout, stage, BYTE_OFFSET, sizeof(PushConstant), &pc
    );

    uint32_t constexpr WORKGROUP_SIZE{16};

    vkCmdDispatch(
        cmd,
        detail::computeDispatchCount(drawRect.extent.width, WORKGROUP_SIZE),
        detail::computeDispatchCount(drawRect.extent.height, WORKGROUP_SIZE),
        1
    );
}
} // namespace vkt