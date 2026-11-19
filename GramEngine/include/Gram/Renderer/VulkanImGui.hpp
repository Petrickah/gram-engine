#pragma once

#include "Gram/Renderer/VulkanPass.hpp"

GRAM_NAMESPACE_BEGIN

struct ImGuiApplicationLayer;
struct QueueFamilyIndices;
struct VulkanRendererSpecification;

#if defined(GRAM_VULKAN_RENDERER) && defined(GRAM_IMGUI_ENABLED)
class VulkanImGui : public VulkanPass, public LayerOwner<ImGuiApplicationLayer>
{
public:
    const char* GetName() const override { return "ImGui"; }
    VulkanImGui(
        const VulkanRendererSpecification* rendererSpec,
        const vk::Instance&                instance,
        const vk::PhysicalDevice&          physicalDevice,
        const vk::Device&                  device,
        const vk::SurfaceFormatKHR&        swapChainFormat,
        QueueFamilyIndices&                queueFamilyIndices,
        uint32_t                           imageCount
    );

    bool Record(vk::CommandBuffer cmd, VulkanPass::Context ctx) override;
    bool OnPresented() override;
    bool Finish() override;

private:
    using super = LayerOwner<ImGuiApplicationLayer>;

private:
    vk::UniqueDescriptorPool  m_ImGuiPool;
    std::string               m_IniPath;
};
#endif // #if defined(GRAM_VULKAN_RENDERER) && defined(GRAM_IMGUI_ENABLED)

GRAM_NAMESPACE_END
