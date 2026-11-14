#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#ifdef GRAM_EDITOR_ENABLED
GRAM_NAMESPACE_BEGIN

class World;

// Shows every component of the selected entity, each drawn from its descriptor: a new
// component or field needs no code here, only a case per FieldType.
class InspectorPanel : public EditorPanel
{
public:
    InspectorPanel(World& world) : EditorPanel("Inspector"), m_World(world) { }

    void Select(ObjectID entity) { m_Selected = entity; }
    ObjectID GetSelected() const { return m_Selected; }

private:
    void DrawContents() override;
    static void DrawComponent(const ComponentDescriptor& desc, void* data);

private:
    World&   m_World;
    ObjectID m_Selected = InvalidObjectID;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_EDITOR_ENABLED
