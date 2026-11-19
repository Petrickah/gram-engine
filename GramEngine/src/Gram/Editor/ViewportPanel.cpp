#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Editor/ViewportPanel.hpp"

#if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
#include <imgui_impl_vulkan.h>

GRAM_NAMESPACE_BEGIN

void ViewportPanel::DrawContents()
{
    // Measured now, while ImGui lays the panel out: the passes of this frame already rendered, so
    // the target takes the new size at the start of the next frame. Shown stretched for that frame.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    m_Target.RequestExtent(vk::Extent2D { static_cast<uint32_t>(available.x), static_cast<uint32_t>(available.y) });

    RefreshTexture();
    if (!m_Texture) return;

    // A fixed resolution (picked in a settings panel) may not match the panel: shown as large as fits,
    // centered, its aspect kept (letterboxed); otherwise the image is the panel's own size.
    ImVec2 size = available;
    if (const std::optional<vk::Extent2D> fixed = m_Target.GetFixedExtent())
    {
        const float scale = std::min(available.x / static_cast<float>(fixed->width), available.y / static_cast<float>(fixed->height));
        size = ImVec2(static_cast<float>(fixed->width) * scale, static_cast<float>(fixed->height) * scale);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 0.5f * (available.x - size.x), ImGui::GetCursorPosY() + 0.5f * (available.y - size.y)));
    }
    ImGui::Image(reinterpret_cast<ImTextureID>(m_Texture), size);
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
#endif // #if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
