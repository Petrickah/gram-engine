#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/EditorToolbar.hpp"

#ifdef GRAM_EDITOR_ENABLED
#include <imgui_internal.h>   // BeginViewportSideBar

GRAM_NAMESPACE_BEGIN

namespace
{
    enum class Icon { Play, Pause, Stop };

    // A button with a shape drawn on it (the default font has no ▶). `active` keeps it highlighted:
    // the state it stands for is the current one. A disabled button never reports a press.
    bool IconButton(const char* id, Icon icon, const char* tooltip, bool enabled, bool active)
    {
        const float height = ImGui::GetFrameHeight();

        ImGui::BeginDisabled(!enabled);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool pressed = ImGui::Button(id, ImVec2(height * 1.6f, height));
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);

        // GetColorU32 applies the style's alpha, which BeginDisabled lowers: a disabled icon fades too.
        const ImU32  color  = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 min    = ImGui::GetItemRectMin();
        const ImVec2 max    = ImGui::GetItemRectMax();
        const ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float  r      = height * 0.25f;
        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        switch (icon)
        {
        case Icon::Play:
            draw->AddTriangleFilled(ImVec2(center.x - r * 0.8f, center.y - r), ImVec2(center.x - r * 0.8f, center.y + r), ImVec2(center.x + r, center.y), color);
            break;
        case Icon::Pause:
            draw->AddRectFilled(ImVec2(center.x - r, center.y - r), ImVec2(center.x - r * 0.3f, center.y + r), color);
            draw->AddRectFilled(ImVec2(center.x + r * 0.3f, center.y - r), ImVec2(center.x + r, center.y + r), color);
            break;
        case Icon::Stop:
            draw->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.85f), ImVec2(center.x + r * 0.85f, center.y + r * 0.85f), color);
            break;
        }
        ImGui::EndDisabled();

        return pressed;
    }

    const char* StateName(GameAPILoader::ReplayState state)
    {
        switch (state)
        {
        case GameAPILoader::ReplayState::Stopped:  return "Stopped";
        case GameAPILoader::ReplayState::Building: return "Building...";
        case GameAPILoader::ReplayState::Playing:  return "Playing";
        case GameAPILoader::ReplayState::Paused:   return "Paused";
        }
        return "";
    }
}

bool EditorToolbar::OnDrawFrame()
{
    using State = GameAPILoader::ReplayState;
    const State state = m_Loader.GetReplayState();

    const ImVec2 padding(8.0f, 4.0f);
    const float  height = ImGui::GetFrameHeight() + 2.0f * padding.y;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    const bool visible = ImGui::BeginViewportSideBar("##Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags);
    ImGui::PopStyleVar();
    if (visible)
    {
        // The three buttons centered, the state to their right.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttons = 3.0f * ImGui::GetFrameHeight() * 1.6f + 2.0f * style.ItemSpacing.x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttons) * 0.5f);

        const bool stopped = state == State::Stopped;
        const bool playing = state == State::Playing;
        const bool paused  = state == State::Paused;
        // While playing, Play rebuilds and reloads the game's code without stopping it.
        const char* playTooltip = playing ? "Play: build the game's code again and reload it, without stopping"
                                          : "Play: build the game's code, then run its systems";
        if (IconButton("##Play",  Icon::Play,  playTooltip,                                         stopped || paused || playing, playing || state == State::Building)) m_Loader.Play();
        ImGui::SameLine();
        if (IconButton("##Pause", Icon::Pause, "Pause: hold the systems",                           playing,           paused))                                m_Loader.Pause();
        ImGui::SameLine();
        if (IconButton("##Stop",  Icon::Stop,  "Stop: put the world back to how it was at Play",    playing || paused, false))                                 m_Loader.Stop();

        ImGui::SameLine(0.0f, 2.0f * style.ItemSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", StateName(state));
    }
    ImGui::End();   // always, like Begin

    return true;    // false would skip the layers after this one
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_EDITOR_ENABLED
