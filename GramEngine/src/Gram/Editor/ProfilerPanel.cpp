#include "Gram/Renderer/VulkanProfiler.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"
#include <imgui.h>
#include "Gram/Editor/ProfilerPanel.hpp"

#if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
GRAM_NAMESPACE_BEGIN

void ProfilerPanel::DrawContents()
{
    const std::vector<VulkanProfiler::Timing>& timings = m_Renderer.GetProfiler().GetTimings();
    if (timings.empty())
    {
        ImGui::TextDisabled("No GPU timings: timestamps not supported, or nothing measured yet.");
        return;
    }

    float gpuMs = 0.0f;
    for (const VulkanProfiler::Timing& timing : timings) gpuMs += timing.Ms;
    const float frameMs = 1000.0f / ImGui::GetIO().Framerate;

    ImGui::Text("GPU %.2f ms of a %.2f ms frame", gpuMs, frameMs);
    ImGui::TextDisabled("the rest, %.2f ms: CPU work and waiting", std::max(0.0f, frameMs - gpuMs));

    if (ImGui::BeginTable("passes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("ms",   ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Share of the GPU's time");
        ImGui::TableHeadersRow();

        for (const VulkanProfiler::Timing& timing : timings)
        {
            ImGui::TableNextRow();
            ImGui::PushID(timing.Name.c_str());
            ImGui::TableNextColumn();
            if (VulkanPass* pass = m_Renderer.FindPass(timing.Name))
            {
                bool enabled = pass->IsEnabled();
                if (ImGui::Checkbox("##enabled", &enabled)) pass->SetEnabled(enabled);
            }
            else ImGui::TextDisabled("-");   // the engine's own: always on
            ImGui::TableNextColumn(); ImGui::TextUnformatted(timing.Name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.2f", timing.Ms);
            ImGui::TableNextColumn(); ImGui::ProgressBar(gpuMs > 0.0f ? timing.Ms / gpuMs : 0.0f, ImVec2(-1.0f, 0.0f));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
