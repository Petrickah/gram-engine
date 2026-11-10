#pragma once

#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImage.hpp"
#include "vulkan/vulkan.hpp"

#include <cstdint>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDescriptors : private MoveObject
{
public:
    static VulkanDescriptors Create(VulkanDevice& device, uint32_t setCount, std::span<const vk::DescriptorSetLayoutBinding> bindings)
    {
        std::vector<vk::DescriptorPoolSize> poolSizes;
        std::for_each(bindings.begin(), bindings.end(), [&poolSizes, setCount](const vk::DescriptorSetLayoutBinding& binding)
        {
            poolSizes.push_back({
                .type            = binding.descriptorType,
                .descriptorCount = binding.descriptorCount * setCount
            });
        });

        auto logicalDevice  = static_cast<vk::Device>(device.GetLogicalDevice());
        auto descriptorPool = logicalDevice.createDescriptorPoolUnique({
            .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets       = setCount,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes    = poolSizes.data()
        });
        GramVulkanCheck_(descriptorPool.result, "error creating the descriptor pool", return VulkanDescriptors());

        auto descriptorLayout = logicalDevice.createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo
        {
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data(),
        });

        GramVulkanCheck_(descriptorLayout.result, "error creating the descriptor layout", return VulkanDescriptors());

        std::vector<vk::DescriptorSetLayout> layouts(setCount, *descriptorLayout.value);
        auto descriptorSets = logicalDevice.allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo 
        {
            .descriptorPool     = *descriptorPool.value,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts        = layouts.data(),
        });
        
        GramVulkanCheck_(descriptorSets.result, "error creating the descriptor set", return VulkanDescriptors());
        return VulkanDescriptors(std::move(descriptorLayout.value), std::move(descriptorPool.value), std::move(descriptorSets.value), bindings);
    }

    void Write(VulkanDevice& device, uint32_t binding, std::span<const VulkanBuffer> perFrame, vk::DeviceSize range)
    {
        auto logicalDevice = static_cast<vk::Device>(device.GetLogicalDevice());
        for (uint32_t frameIndex = 0; frameIndex < perFrame.size(); frameIndex++)
        {
            vk::DescriptorBufferInfo bufferInfo
            {
                .buffer = perFrame[frameIndex].Get(),
                .offset = 0,
                .range  = range
            };

            logicalDevice.updateDescriptorSets(vk::WriteDescriptorSet
            {
                .dstSet          = Get(frameIndex),
                .dstBinding      = binding,
                .descriptorCount = 1,
                .descriptorType  = GetType(binding),
                .pBufferInfo     = &bufferInfo
            }, {});
        }
    }

    void Write(VulkanDevice& device, uint32_t binding, const VulkanImage& image)
    {
        auto logicalDevice = static_cast<vk::Device>(device.GetLogicalDevice());
        vk::DescriptorImageInfo imageInfo
        {
            .sampler = image.GetSampler(),
            .imageView = image.GetView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        for (uint32_t setIndex = 0; setIndex < m_Sets.size(); setIndex++)
        {
            logicalDevice.updateDescriptorSets(vk::WriteDescriptorSet {
                .dstSet          = *m_Sets[setIndex],
                .dstBinding      = binding,
                .descriptorCount = 1,
                .descriptorType  = GetType(binding),
                .pImageInfo      = &imageInfo
            }, {});
        }
    }

    vk::DescriptorSetLayout GetLayout()               const { return *m_Layout;                           }
    vk::DescriptorSet       Get(uint32_t frameIndex)  const { return *m_Sets[frameIndex];                 }
    vk::DescriptorType      GetType(uint32_t binding) const
    {
        auto it = std::ranges::find(m_Bindings, binding, &vk::DescriptorSetLayoutBinding::binding);
        return it != m_Bindings.end() ? it->descriptorType : vk::DescriptorType{};
    }

private:
    VulkanDescriptors() = default;
    VulkanDescriptors(vk::UniqueDescriptorSetLayout layout, vk::UniqueDescriptorPool pool, std::vector<vk::UniqueDescriptorSet> sets, std::span<const vk::DescriptorSetLayoutBinding> bindings)
        : m_Layout(std::move(layout))
        , m_Pool(std::move(pool))
        , m_Sets(std::move(sets))
        , m_Bindings(bindings.begin(), bindings.end())
    {}

private:
    vk::UniqueDescriptorSetLayout               m_Layout;
    vk::UniqueDescriptorPool                    m_Pool;
    std::vector<vk::UniqueDescriptorSet>        m_Sets;
    std::vector<vk::DescriptorSetLayoutBinding> m_Bindings;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER