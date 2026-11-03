#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

class World;
class InspectorPanel;

// Every live entity of the World; clicking one selects it in the inspector.
class OutlinerPanel : public EditorPanel
{
public:
    OutlinerPanel(World& world, InspectorPanel& inspector) : EditorPanel("Outliner"), m_World(world), m_Inspector(inspector) { }

private:
    void DrawContents() override;

private:
    World&          m_World;
    InspectorPanel& m_Inspector;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
