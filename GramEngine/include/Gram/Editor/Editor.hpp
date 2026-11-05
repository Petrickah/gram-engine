#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

class World;
class GameAPILoader;
class InspectorPanel;
class LoggerPanel;
class OutlinerPanel;
class SystemsPanel;

// The main menu bar; its Window menu closes and reopens every panel added to the editor.
class EditorMenuBar : public ImGuiApplicationLayer
{
public:
    void Add(EditorPanel& panel) { m_Panels.push_back(&panel); }

private:
    bool OnDrawFrame() override;

private:
    std::vector<EditorPanel*> m_Panels;   // owned by the same LayerOwner as the menu bar
};

// The editor every game gets: the menu bar plus the Outliner, Inspector and Systems panels, as
// ImGui layers of `ui`. Create it after the World and the loader: the panels keep references.
class Editor : private UniqueObject
{
public:
    Editor(LayerOwner<ImGuiApplicationLayer>& ui, World& world, GameAPILoader& loader);

    // A game's own panel, listed in the Window menu like the editor's.
    template<class Panel, class... Args>
        requires std::derived_from<Panel, EditorPanel>
    std::shared_ptr<Panel> AddPanel(Args&&... args)
    {
        std::shared_ptr<Panel> panel;
        if (!m_UI.RegisterLayer<Panel>(panel, std::forward<Args>(args)...)) return nullptr;

        m_MenuBar->Add(*panel);
        return panel;
    }

    InspectorPanel& GetInspector() { return *m_Inspector; }

private:
    LayerOwner<ImGuiApplicationLayer>& m_UI;
    std::shared_ptr<EditorMenuBar>     m_MenuBar;
    std::shared_ptr<OutlinerPanel>     m_Outliner;
    std::shared_ptr<InspectorPanel>    m_Inspector;
    std::shared_ptr<LoggerPanel>       m_Logger;
    std::shared_ptr<SystemsPanel>      m_Systems;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
