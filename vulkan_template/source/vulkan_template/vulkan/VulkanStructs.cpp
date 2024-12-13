#include "VulkanStructs.hpp"

namespace vkt
{
auto fenceCreateInfo(VkFenceCreateFlags const flags) -> VkFenceCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
    };
}

auto semaphoreCreateInfo(VkSemaphoreCreateFlags const flags)
    -> VkSemaphoreCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
    };
}

auto commandBufferBeginInfo(VkCommandBufferUsageFlags const flags)
    -> VkCommandBufferBeginInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = flags,
        .pInheritanceInfo = nullptr,
    };
}

auto imageSubresourceRange(VkImageAspectFlags const aspectMask)
    -> VkImageSubresourceRange
{
    return {
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
}

auto imageSubresourceLayers(
    VkImageAspectFlags const aspectMask,
    uint32_t const mipLevel,
    uint32_t const baseArrayLayer,
    uint32_t const baseArrayCount
) -> VkImageSubresourceLayers
{
    return {
        .aspectMask = aspectMask,
        .mipLevel = mipLevel,
        .baseArrayLayer = baseArrayLayer,
        .layerCount = baseArrayCount,
    };
}

auto semaphoreSubmitInfo(
    VkPipelineStageFlags2 const stageMask, VkSemaphore const semaphore
) -> VkSemaphoreSubmitInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = semaphore,
        .value = 1,
        .stageMask = stageMask,
        .deviceIndex = 0, // Assume single device, at index 0.
    };
}

auto commandBufferSubmitInfo(VkCommandBuffer const cmd)
    -> VkCommandBufferSubmitInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = cmd,
        .deviceMask = 0,
    };
}

auto submitInfo(
    std::vector<VkCommandBufferSubmitInfo> const& cmdInfo,
    std::vector<VkSemaphoreSubmitInfo> const& waitSemaphoreInfo,
    std::vector<VkSemaphoreSubmitInfo> const& signalSemaphoreInfo
) -> VkSubmitInfo2
{
    return {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,

        .flags = 0,

        .waitSemaphoreInfoCount =
            static_cast<uint32_t>(waitSemaphoreInfo.size()),
        .pWaitSemaphoreInfos = waitSemaphoreInfo.data(),

        .commandBufferInfoCount = static_cast<uint32_t>(cmdInfo.size()),
        .pCommandBufferInfos = cmdInfo.data(),

        .signalSemaphoreInfoCount =
            static_cast<uint32_t>(signalSemaphoreInfo.size()),
        .pSignalSemaphoreInfos = signalSemaphoreInfo.data(),
    };
}

auto imageCreateInfo(
    VkFormat const format,
    VkImageLayout const initialLayout,
    VkImageUsageFlags const usageMask,
    VkExtent3D const extent,
    VkImageTiling const tiling
) -> VkImageCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .imageType = VK_IMAGE_TYPE_2D,

        .format = format,
        .extent = extent,

        .mipLevels = 1,
        .arrayLayers = 1,

        .samples = VK_SAMPLE_COUNT_1_BIT,

        .tiling = tiling,
        .usage = usageMask,

        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,

        .initialLayout = initialLayout,
    };
}

auto samplerCreateInfo(
    VkSamplerCreateFlags const flags,
    VkBorderColor const borderColor,
    VkFilter const filter,
    VkSamplerAddressMode const addressMode
) -> VkSamplerCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,

        .flags = flags,

        .magFilter = filter,
        .minFilter = filter,

        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,

        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,

        .mipLodBias = 0.0F,

        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0F,

        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,

        .minLod = 0.0F,
        .maxLod = 1.0F,

        .borderColor = borderColor,

        .unnormalizedCoordinates = VK_FALSE,
    };
}

auto imageViewCreateInfo(
    VkFormat const format,
    VkImage const image,
    VkImageAspectFlags const aspectFlags
) -> VkImageViewCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,

        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = imageSubresourceRange(aspectFlags),
    };
}

auto renderingAttachmentInfo(
    VkImageView const view,
    VkImageLayout const layout,
    std::optional<VkClearValue> const clearValue
) -> VkRenderingAttachmentInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,

        .imageView = view,
        .imageLayout = layout,
        .loadOp = clearValue.has_value() ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                         : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearValue.value_or(VkClearValue{}),
    };
}

auto renderingInfo(
    VkRect2D const drawRect,
    std::span<VkRenderingAttachmentInfo const> const colorAttachments,
    VkRenderingAttachmentInfo const* const pDepthAttachment
) -> VkRenderingInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,

        .flags = 0,
        .renderArea = drawRect,
        .layerCount = 1,
        .viewMask = 0,

        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),

        .pDepthAttachment = pDepthAttachment,
        .pStencilAttachment = nullptr,
    };
}

auto pipelineShaderStageCreateInfo(
    VkShaderStageFlagBits const stage,
    VkShaderModule const module,
    char const* const entryPoint
) -> VkPipelineShaderStageCreateInfo
{
    return VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,

        .flags = 0,
        .stage = stage,
        .module = module,
        .pName = entryPoint,
        .pSpecializationInfo = nullptr,
    };
}

auto pipelineLayoutCreateInfo(
    VkPipelineLayoutCreateFlags const flags,
    std::span<VkDescriptorSetLayout const> const layouts,
    std::span<VkPushConstantRange const> const ranges
) -> VkPipelineLayoutCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,

        .flags = flags,

        .setLayoutCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),

        .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    };
}
} // namespace vkt