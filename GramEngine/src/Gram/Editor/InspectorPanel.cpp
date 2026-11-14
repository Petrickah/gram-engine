#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Editor/InspectorPanel.hpp"

#ifdef GRAM_EDITOR_ENABLED
GRAM_NAMESPACE_BEGIN

void InspectorPanel::DrawContents()
{
    if (!m_World.IsAlive(m_Selected))
    {
        ImGui::TextDisabled("no entity selected");
        return;
    }

    ImGui::Text("Entity %u (generation %u)", m_Selected.Index, m_Selected.Generation);
    m_World.ForEachComponent(m_Selected, [](const ComponentDescriptor& desc, void* data) { DrawComponent(desc, data); });
}

void InspectorPanel::DrawComponent(const ComponentDescriptor& desc, void* data)
{
    if (!ImGui::CollapsingHeader(desc.Name, ImGuiTreeNodeFlags_DefaultOpen)) return;

    for (const FieldDescriptor& field : std::span<const FieldDescriptor>(desc.Fields, desc.Count))
    {
        void* value = static_cast<std::byte*>(data) + field.Offset;
        switch (field.Type)
        {
            default: ImGui::TextDisabled("%s: unsupported type", field.Name); break;
            case FieldDescriptor::FieldType::Str:  ImGui::InputText(field.Name, static_cast<char*>(value), field.Size); break;
            case FieldDescriptor::FieldType::F32:  ImGui::DragFloat(field.Name, static_cast<float*>(value)); break;
            case FieldDescriptor::FieldType::I32:  ImGui::DragScalar(field.Name, ImGuiDataType_S32, value); break;
            case FieldDescriptor::FieldType::U32:  ImGui::DragScalar(field.Name, ImGuiDataType_U32, value); break;
            case FieldDescriptor::FieldType::Bool: ImGui::Checkbox(field.Name, static_cast<bool*>(value)); break;
        }
    }
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_EDITOR_ENABLED
