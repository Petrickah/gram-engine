#pragma once
#ifdef GRAM_VULKAN_RENDERER

#include "Renderer.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanImGui : private UniqueObject
{
public:
    VulkanImGui(
        const VulkanRendererSpecification* rendererSpec,
        const vk::Instance&                instance,
        const vk::PhysicalDevice&          physicalDevice,
        const vk::Device&                  device,
        const vk::SurfaceFormatKHR&        swapChainFormat,
        QueueFamilyIndices&                queueFamilyIndices,
        uint32_t                           imageCount
    );

    void RenderFrame(const vk::CommandBuffer& commandBuffer);
    void Finish();

private:
    vk::UniqueDescriptorPool  m_ImGuiPool;
    std::string               m_IniPath;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER