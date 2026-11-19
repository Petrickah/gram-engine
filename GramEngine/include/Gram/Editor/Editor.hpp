#pragma once

#include "Gram/Common.hpp"
#include "Gram/Editor/EditorPanel.hpp"
#include <memory>

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

class World;
class GameAPILoader;
class EditorToolbar;
class InspectorPanel;
class LoggerPanel;
class OutlinerPanel;
class ProfilerPanel;
class VulkanProfiler;
class SystemsPanel;
class ViewportPanel;
class VulkanRenderTarget;

// Who a panel belongs to: the engine's are the same for every game and exist only where the game is
// written (Debug, GRAM_EDITOR_ENABLED); a game's own are also there in Release, to test the game with.
enum class PanelOwner { Engine, Game };

// The main menu bar: one menu per owner ("Engine", "Game") closing and reopening its panels, each in
// the submenus its path names, and the frame rate.
class EditorMenuBar : public ImGuiApplicationLayer
{
public:
    void Add(EditorPanel& panel, PanelOwner owner) { (owner == PanelOwner::Engine ? m_EnginePanels : m_GamePanels).push_back(&panel); }

private:
    bool OnDrawFrame() override;

private:
    // Owned by the same LayerOwner as the menu bar.
    std::vector<EditorPanel*> m_EnginePanels;
    std::vector<EditorPanel*> m_GamePanels;
};

// Every FunctionPanel, drawn by one layer: a LayerOwner takes a single layer of each type, so the
// panels themselves can't be layers.
class EditorFunctionPanels : public ImGuiApplicationLayer
{
public:
    using Func = std::function<void()>;

    EditorFunctionPanels(EditorMenuBar& menu) : m_Menu(menu) {}

    FunctionPanel& Add(PanelOwner owner, std::string_view path, Func callback)
    {
        m_Panels.push_back(std::make_unique<FunctionPanel>(path, std::move(callback)));
        m_Menu.Add(*m_Panels.back(), owner);
        return *m_Panels.back();
    }

private:
    bool OnDrawFrame() override;

private:
    EditorMenuBar& m_Menu;
    std::vector<std::unique_ptr<FunctionPanel>> m_Panels;
};

// The ImGui host every game gets: the menu bar and, in Debug only (GRAM_EDITOR_ENABLED), the engine's
// panels: Outliner, Inspector, Systems, Log, and a Viewport showing `sceneTarget` when one is given.
// Everything a game adds through AddPanel is a game panel, there in Debug and Release. Create it after the
// World and the loader: the engine's panels keep references.
class Editor : private UniqueObject
{
public:
    using Func = std::function<void()>;
    Editor(LayerOwner<ImGuiApplicationLayer>& ui, World& world, GameAPILoader& loader, std::shared_ptr<VulkanRenderTarget> sceneTarget = nullptr, const VulkanProfiler* profiler = nullptr);

    // A game's own panel drawn by a function, listed in the Game menu under its path ("Debug/Ray Tracer").
    FunctionPanel& AddPanel(std::string_view path, Func draw)
    {
        return m_FunctionPanels->Add(PanelOwner::Game, path, std::move(draw));
    }

    // A game's own panel, listed in the Game menu.
    template<class Panel, class... Args>
        requires std::derived_from<Panel, EditorPanel>
    std::shared_ptr<Panel> AddPanel(Args&&... args)
    {
        return Register<Panel>(PanelOwner::Game, std::forward<Args>(args)...);
    }

#ifdef GRAM_EDITOR_ENABLED
    InspectorPanel& GetInspector() { return *m_Inspector; }
#endif // #ifdef GRAM_EDITOR_ENABLED

private:
    template<class Panel, class... Args>
        requires std::derived_from<Panel, EditorPanel>
    std::shared_ptr<Panel> Register(PanelOwner owner, Args&&... args)
    {
        std::shared_ptr<Panel> panel;
        if (!m_UI.RegisterLayer<Panel>(panel, std::forward<Args>(args)...)) return nullptr;

        m_MenuBar->Add(*panel, owner);
        return panel;
    }

private:
    LayerOwner<ImGuiApplicationLayer>&    m_UI;
    std::shared_ptr<EditorMenuBar>        m_MenuBar;
    std::shared_ptr<EditorFunctionPanels> m_FunctionPanels;
#ifdef GRAM_EDITOR_ENABLED
    std::shared_ptr<EditorToolbar>        m_Toolbar;
    std::shared_ptr<OutlinerPanel>        m_Outliner;
    std::shared_ptr<InspectorPanel>       m_Inspector;
    std::shared_ptr<LoggerPanel>          m_Logger;
    std::shared_ptr<SystemsPanel>         m_Systems;
    std::shared_ptr<ViewportPanel>        m_Viewport;
    std::shared_ptr<ProfilerPanel>        m_Profiler;
#endif // #ifdef GRAM_EDITOR_ENABLED
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
