#include "Gram/Scripting/Common.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Editor/EditorToolbar.hpp"
#include "Gram/Editor/InspectorPanel.hpp"
#include "Gram/Editor/LoggerPanel.hpp"
#include "Gram/Editor/OutlinerPanel.hpp"
#include "Gram/Editor/SystemsPanel.hpp"
#include "Gram/Editor/ViewportPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

bool EditorMenuBar::OnDrawFrame()
{
    if (ImGui::BeginMainMenuBar())
    {
        // A menu per owner, only when it has panels (Release has no engine panels).
        auto menu = [](const char* name, const std::vector<EditorPanel*>& panels)
        {
            if (panels.empty() || !ImGui::BeginMenu(name)) return;

            for (EditorPanel* panel : panels)
                ImGui::MenuItem(panel->GetName(), nullptr, &panel->IsOpen());

            ImGui::EndMenu();
        };
        menu("Engine", m_EnginePanels);
        menu("Game",   m_GamePanels);

        // The frame rate, right-aligned: ImGui's own running average over the last frames.
        const ImGuiIO& io = ImGui::GetIO();
        char frameRate[64];
        std::snprintf(frameRate, sizeof(frameRate), "%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(frameRate).x - 2.0f * ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextUnformatted(frameRate);

        ImGui::EndMainMenuBar();
    }

    return true;
}

Editor::Editor(LayerOwner<ImGuiApplicationLayer>& ui, [[maybe_unused]] World& world, [[maybe_unused]] GameAPILoader& loader, [[maybe_unused]] std::shared_ptr<VulkanRenderTarget> sceneTarget)
    : m_UI(ui)
{
    GramCheckVoid(m_UI.RegisterLayer<EditorMenuBar>(m_MenuBar), "couldn't create the editor's menu bar");

#ifdef GRAM_EDITOR_ENABLED
    // Right after the menu bar: layers draw in registration order, and the toolbar goes under the menu.
    GramCheckVoid(m_UI.RegisterLayer<EditorToolbar>(m_Toolbar, loader), "couldn't create the editor's toolbar");
    // The inspector first: the outliner selects into it.
    m_Inspector = Register<InspectorPanel>(PanelOwner::Engine, world);
    GramCheckVoid(m_Inspector, "couldn't create the Inspector");
    m_Outliner  = Register<OutlinerPanel>(PanelOwner::Engine, world, *m_Inspector);
    m_Systems   = Register<SystemsPanel>(PanelOwner::Engine, loader);
#ifdef GRAM_LOGGER_ENABLED
    m_Logger    = Register<LoggerPanel>(PanelOwner::Engine);
#endif // #ifdef GRAM_LOGGER_ENABLED
#ifdef GRAM_VULKAN_RENDERER
    if (sceneTarget) m_Viewport = Register<ViewportPanel>(PanelOwner::Engine, *sceneTarget);
#endif // #ifdef GRAM_VULKAN_RENDERER
#endif // #ifdef GRAM_EDITOR_ENABLED
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
