#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
GRAM_NAMESPACE_BEGIN

class VulkanRenderTarget;

// The scene, as the game's passes rendered it into the renderer's scene target. The panel asks the
// target for its own size every frame (applied at the start of the next one) and rebuilds its ImGui
// texture whenever the target recreated its image.
class ViewportPanel : public EditorPanel
{
public:
    ViewportPanel(VulkanRenderTarget& target) : EditorPanel("Viewport"), m_Target(target) { }

private:
    void DrawContents() override;
    // The ImGui texture goes before ImGui_ImplVulkan_Shutdown: VulkanImGui::Finish stops its layers
    // first, after the renderer waited for the device, so the GPU no longer reads it.
    bool OnStop() override;

    void RefreshTexture();

private:
    VulkanRenderTarget& m_Target;
    VkDescriptorSet     m_Texture    = VK_NULL_HANDLE;
    uint64_t            m_Generation = 0;
};

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
