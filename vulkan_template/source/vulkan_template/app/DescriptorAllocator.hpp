#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <optional>
#include <span>
#include <vector>

namespace vkt
{
struct DescriptorLayoutBuilder
{
    struct BindingParams
    {
        VkDescriptorType type;
        VkShaderStageFlags stageMask;
        VkDescriptorBindingFlags bindingFlags;
    };

    // Pushes an additional binding, with binding number after the last.
    auto pushBinding(BindingParams parameters, uint32_t count = 1)
        -> DescriptorLayoutBuilder&;

    // Pushes an additional binding plus immutable samplers, with binding number
    // after the last.
    auto pushBinding(BindingParams parameters, std::vector<VkSampler> samplers)
        -> DescriptorLayoutBuilder&;

    auto
    build(VkDevice device, VkDescriptorSetLayoutCreateFlags layoutFlags) const
        -> std::optional<VkDescriptorSetLayout>;

private:
    struct Binding
    {
        std::vector<VkSampler> immutableSamplers{};
        VkDescriptorSetLayoutBinding binding{};
        VkDescriptorBindingFlags flags{};
    };

    std::vector<Binding> m_bindings{};
};

// Holds a descriptor pool and allows allocating from it.
struct DescriptorAllocator
{
public:
    struct PoolSizeRatio
    {
        VkDescriptorType type{VK_DESCRIPTOR_TYPE_SAMPLER};
        float ratio{0.0F};
    };

    DescriptorAllocator() = delete;

    DescriptorAllocator(DescriptorAllocator const&) = delete;
    auto operator=(DescriptorAllocator const&) -> DescriptorAllocator& = delete;

    DescriptorAllocator(DescriptorAllocator&&) noexcept;
    auto operator=(DescriptorAllocator&&) noexcept -> DescriptorAllocator&;

    ~DescriptorAllocator();

    static auto create(
        VkDevice device,
        uint32_t maxSets,
        std::span<PoolSizeRatio const> poolRatios,
        VkDescriptorPoolCreateFlags flags
    ) -> DescriptorAllocator;
    void clearDescriptors(VkDevice device);

    // Asserts on failure
    // TODO: remove extra device parameter
    auto allocate(VkDevice device, VkDescriptorSetLayout layout)
        -> VkDescriptorSet;

private:
    DescriptorAllocator(VkDevice device, VkDescriptorPool pool)
        : m_device{device}
        , m_pool{pool}
    {
    }

    void destroy() noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_pool{VK_NULL_HANDLE};
};
} // namespace vkt