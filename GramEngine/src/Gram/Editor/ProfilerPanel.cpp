#include "Gram/Renderer/VulkanProfiler.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"
#include "Gram/Platform.hpp"
#include <imgui.h>
#include "Gram/Editor/ProfilerPanel.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
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

    DrawRecording();
}

// Record: a CSV of these timings over time, in benchmarks/ next to the executable (docs/wave-2-benchmarking.md).
void ProfilerPanel::DrawRecording()
{
    ImGui::SeparatorText("Recording");
    const BenchmarkRecorder& recorder = m_Renderer.GetRecorder();

    if (recorder.IsRecording())
    {
        const float elapsed  = recorder.GetElapsed();
        const float duration = recorder.GetDuration();
        const std::string progress = std::format("{:.0f} / {:.0f} s", elapsed, duration);
        ImGui::ProgressBar(duration > 0.0f ? elapsed / duration : 0.0f, ImVec2(-1.0f, 0.0f), progress.c_str());
        if (ImGui::Button("Stop")) m_Renderer.StopRecording();
    }
    else
    {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Row every (s)", &m_RecordInterval, 0.1f, 1.0f, "%.1f");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Duration (s)", &m_RecordDuration, 5.0f, 30.0f, "%.0f");
        m_RecordInterval = std::clamp(m_RecordInterval, 0.1f, 60.0f);
        m_RecordDuration = std::clamp(m_RecordDuration, m_RecordInterval, 3600.0f);

        if (ImGui::Button("Record")) m_Renderer.StartRecording(GetExecutableDirectory() / "benchmarks", m_RecordInterval, m_RecordDuration);
        ImGui::SameLine();
        ImGui::TextDisabled("fix the resolution and turn V-Sync off first (Debug/Display)");
    }

    if (!recorder.GetPath().empty()) ImGui::TextDisabled("%s", recorder.GetPath().string().c_str());
}

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
