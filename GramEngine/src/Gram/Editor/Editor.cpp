#include "Gram/Editor/EditorPanel.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Editor/EditorToolbar.hpp"
#include "Gram/Editor/InspectorPanel.hpp"
#include "Gram/Editor/LoggerPanel.hpp"
#include "Gram/Editor/OutlinerPanel.hpp"
#include "Gram/Editor/ProfilerPanel.hpp"
#include "Gram/Editor/SystemsPanel.hpp"
#include "Gram/Editor/ViewportPanel.hpp"
#include "Gram/Logging/GramLogger.hpp"

#ifdef GRAM_IMGUI_ENABLED

#include <GLFW/glfw3.h>
#include <imgui.h>

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
            {
                // A submenu per segment but the last, the item itself last. A closed submenu returns
                // false and takes no EndMenu: close only the ones that opened.
                const std::vector<std::string>& segments = panel->GetSegments();
                
                size_t opened       = 0;
                size_t segmentCount = segments.size() - 1;
                for (size_t index = 0; index < segmentCount; index++)
                {
                    const char* label = segments[index].c_str();
                    if (ImGui::BeginMenu(label)) opened++;
                    else break;
                }

                if (opened == segmentCount)
                {
                    ImGui::MenuItem(panel->GetTitle().c_str(), nullptr, &panel->IsOpen());
                }

                for (size_t index = 0; index < opened; index++) ImGui::EndMenu();
            }

            ImGui::EndMenu();
        };
        menu("Engine", m_EnginePanels);
        menu("Game",   m_GamePanels);

        // The frame rate, right-aligned: ImGui's own running average over the last frames.
        const ImGuiIO& io = ImGui::GetIO();
        char frameRate[96];
        int  written = 0;
#ifdef GRAM_VULKAN_RENDERER
        if (m_SceneTarget)   // the resolution the scene renders at: the viewport's, or a fixed one
        {
            const vk::Extent2D extent = m_SceneTarget->GetImage().GetExtent();
            written = std::snprintf(frameRate, sizeof(frameRate), "%ux%u   ", extent.width, extent.height);
        }
#endif // #ifdef GRAM_VULKAN_RENDERER
        std::snprintf(frameRate + written, sizeof(frameRate) - written, "%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

        float windowWidth         = ImGui::GetWindowWidth();
        float itemMargin          = 2.0f * ImGui::GetStyle().ItemSpacing.x;
        float frameRateWidth      = ImGui::CalcTextSize(frameRate).x;
        float closeButtonWidth    = ImGui::CalcTextSize("X").x + 2.0f * ImGui::GetStyle().FramePadding.x;
        float closeButtonPosition = windowWidth - itemMargin - closeButtonWidth;
        float frameRatePosition   = closeButtonPosition - itemMargin - frameRateWidth;

        ImGui::SameLine(frameRatePosition);
        ImGui::TextUnformatted(frameRate);

        GLFWwindow* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
        ImGui::SameLine(closeButtonPosition);
        if (ImGui::SmallButton("X")) glfwSetWindowShouldClose(window, GLFW_TRUE);

        ImGui::EndMainMenuBar();
    }

    return true;
}

bool EditorFunctionPanels::OnDrawFrame()
{
    for (std::unique_ptr<FunctionPanel>& panel : m_Panels)
    {
        if (panel) panel->OnDrawFrame();
    }

    return true;
}

Editor::Editor(LayerOwner<ImGuiApplicationLayer>& ui, [[maybe_unused]] World& world, [[maybe_unused]] GameAPILoader& loader, [[maybe_unused]] std::shared_ptr<VulkanRenderTarget> sceneTarget, [[maybe_unused]] const VulkanProfiler* profiler)
    : m_UI(ui)
{
    GramCheckVoid(m_UI.RegisterLayer<EditorMenuBar>(m_MenuBar, sceneTarget), "couldn't create the editor's menu bar");

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
    if (profiler)    m_Profiler = Register<ProfilerPanel>(PanelOwner::Engine, *profiler);
#endif // #ifdef GRAM_VULKAN_RENDERER
#endif // #ifdef GRAM_EDITOR_ENABLED

    // After the toolbar: windows drawn before it would be placed in the work area it hasn't shrunk yet.
    GramCheckVoid(m_UI.RegisterLayer<EditorFunctionPanels>(m_FunctionPanels, *m_MenuBar), "couldn't create the editor's function panels");
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
