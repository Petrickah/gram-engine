#include "Gram/Scripting/Common.hpp"
#include "Gram/Editor/Editor.hpp"
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
        if (ImGui::BeginMenu("Window"))
        {
            for (EditorPanel* panel : m_Panels)
                ImGui::MenuItem(panel->GetName(), nullptr, &panel->IsOpen());

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    return true;
}

Editor::Editor(LayerOwner<ImGuiApplicationLayer>& ui, World& world, GameAPILoader& loader, std::shared_ptr<VulkanRenderTarget> sceneTarget)
    : m_UI(ui)
{
    GramCheckVoid(m_UI.RegisterLayer<EditorMenuBar>(m_MenuBar), "couldn't create the editor's menu bar");

    // The inspector first: the outliner selects into it.
    m_Inspector = AddPanel<InspectorPanel>(world);
    GramCheckVoid(m_Inspector, "couldn't create the Inspector");
    m_Outliner  = AddPanel<OutlinerPanel>(world, *m_Inspector);
    m_Systems   = AddPanel<SystemsPanel>(loader);
#ifdef GRAM_LOGGER_ENABLED
    m_Logger    = AddPanel<LoggerPanel>();
#endif // #ifdef GRAM_LOGGER_ENABLED
#ifdef GRAM_VULKAN_RENDERER
    if (sceneTarget) m_Viewport = AddPanel<ViewportPanel>(*sceneTarget);
#endif // #ifdef GRAM_VULKAN_RENDERER
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
