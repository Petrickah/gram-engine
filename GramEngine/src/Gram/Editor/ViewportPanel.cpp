#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Editor/ViewportPanel.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
#include <imgui_impl_vulkan.h>

GRAM_NAMESPACE_BEGIN

void ViewportPanel::DrawContents()
{
    // Measured now, while ImGui lays the panel out: the passes of this frame already rendered, so
    // the target takes the new size at the start of the next frame. Shown stretched for that frame.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    m_Target.RequestExtent(vk::Extent2D { static_cast<uint32_t>(available.x), static_cast<uint32_t>(available.y) });

    RefreshTexture();
    if (m_Texture) ImGui::Image(reinterpret_cast<ImTextureID>(m_Texture), available);
}

void ViewportPanel::RefreshTexture()
{
    // A recreated image has a new view, so the descriptor ImGui samples it through is stale. The
    // target waited for the device before recreating, so the old descriptor is no longer in use.
    if (m_Texture && m_Generation == m_Target.GetGeneration()) return;

    if (m_Texture) ImGui_ImplVulkan_RemoveTexture(m_Texture);

    VulkanImage& image = m_Target.GetImage();
    m_Texture    = ImGui_ImplVulkan_AddTexture(image.GetSampler(), image.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_Generation = m_Target.GetGeneration();
}

bool ViewportPanel::OnStop()
{
    if (m_Texture) ImGui_ImplVulkan_RemoveTexture(m_Texture);
    m_Texture = VK_NULL_HANDLE;
    return true;
}

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
