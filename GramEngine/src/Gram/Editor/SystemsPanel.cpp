#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/SystemsPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

// Always true, like InspectorPanel: a closed panel just stops drawing.
bool SystemsPanel::OnDrawFrame()
{
    if (!m_IsOpen) return true;

    if (ImGui::Begin("Systems", &m_IsOpen, ImGuiWindowFlags_None))
    {
        if (ImGui::Button("Reload")) m_Loader.Reload();
        ImGui::SameLine();
        ImGui::TextDisabled(m_Loader.GetGameAPI() ? "game loaded" : "no game loaded");

        const std::span<const System::Descriptor> systems = m_Loader.GetSystems();
        ImGui::Text("%zu system(s)", systems.size());

        if (ImGui::BeginTable("systems", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Event");
            ImGui::TableSetupColumn("Stage");
            ImGui::TableHeadersRow();
            for (const System::Descriptor& system : systems)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(system.Name);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(system.Event);
                ImGui::TableNextColumn();
                switch (system.Stage)
                {
                    case Gram::System::Stage::Event:
                        ImGui::TextUnformatted("Stage::Event");
                        break;
                    case Gram::System::Stage::Update:
                        ImGui::TextUnformatted("Stage::Update");
                        break;
                    default:
                        ImGui::TextUnformatted("Stage::Unknown");
                        break;
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
