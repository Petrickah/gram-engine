#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Editor/InspectorPanel.hpp"
#include "Gram/Editor/OutlinerPanel.hpp"

#ifdef GRAM_EDITOR_ENABLED
GRAM_NAMESPACE_BEGIN

void OutlinerPanel::DrawContents()
{
    const ObjectID selected = m_Inspector.GetSelected();
    m_World.ForEachEntity([&](ObjectID entity)
    {
        // No Name component yet: an entity is shown by its index and the components it has.
        std::string label = std::format("Entity {}", entity.Index);
        const char* separator = " — ";
        m_World.ForEachComponent(entity, [&](const ComponentDescriptor& desc, void*)
        {
            label += separator;
            label += desc.Name;
            separator = ", ";
        });

        ImGui::PushID(static_cast<int>(entity.Index));
        const bool isSelected = entity.Index == selected.Index && entity.Generation == selected.Generation;
        if (ImGui::Selectable(label.c_str(), isSelected)) m_Inspector.Select(entity);
        ImGui::PopID();
    });
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_EDITOR_ENABLED
