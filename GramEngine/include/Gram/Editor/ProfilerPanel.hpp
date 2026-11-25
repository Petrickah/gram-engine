#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
GRAM_NAMESPACE_BEGIN

class VulkanRenderer;

// The GPU time of each render pass last frame (the renderer's VulkanProfiler, smoothed), a bar each, and
// their sum next to the whole frame's time: what's left is the CPU's share and the waiting.
class ProfilerPanel : public EditorPanel
{
public:
    ProfilerPanel(VulkanRenderer& renderer) : EditorPanel("Profiler"), m_Renderer(renderer) { }

private:
    void DrawContents() override;
    void DrawRecording();

private:
    VulkanRenderer& m_Renderer;   // its profiler for the timings (read only), its passes for the checkboxes

    // The next recording's: seconds per CSV row, and how long it runs.
    float m_RecordInterval = 0.5f;
    float m_RecordDuration = 30.0f;
};

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_VULKAN_RENDERER)
