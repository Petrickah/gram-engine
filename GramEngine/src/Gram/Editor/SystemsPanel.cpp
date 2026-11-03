#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/SystemsPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

void SystemsPanel::DrawContents()
{
    if (ImGui::Button("Reload")) m_Loader.Reload();
    ImGui::SameLine();
    ImGui::TextDisabled(m_Loader.GetGameAPI() ? "game loaded" : "no game loaded");

    const std::span<const GameAPILoader::RegisteredSystem> systems = m_Loader.GetSystems();
    ImGui::Text("%zu system(s)", systems.size());

    if (ImGui::BeginTable("systems", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Last run");
        ImGui::TableHeadersRow();
        for (const GameAPILoader::RegisteredSystem& system : systems)
        {
            ImGui::PushID(system.Descriptor.Name);   // the checkboxes have no label: the name tells them apart
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            bool enabled = system.Enabled;
            if (ImGui::Checkbox("##enabled", &enabled)) m_Loader.SetSystemEnabled(system.Descriptor.Name, enabled);

            ImGui::TableNextColumn(); ImGui::TextUnformatted(system.Descriptor.Name);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(system.Descriptor.Event);
            ImGui::TableNextColumn();
            switch (system.Descriptor.Stage)
            {
                case System::Stage::Event:
                    ImGui::TextUnformatted("Stage::Event");
                    break;
                case System::Stage::Update:
                    ImGui::TextUnformatted("Stage::Update");
                    break;
                default:
                    ImGui::TextUnformatted("Stage::Unknown");
                    break;
            }

            ImGui::TableNextColumn();
            if (system.LastRunMs < 0.0f) ImGui::TextDisabled("never");
            else                         ImGui::Text("%.3f ms", system.LastRunMs);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
