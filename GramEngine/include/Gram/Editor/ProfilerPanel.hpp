#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
GRAM_NAMESPACE_BEGIN

class VulkanProfiler;

// The GPU time of each render pass last frame (the renderer's VulkanProfiler, smoothed), a bar each, and
// their sum next to the whole frame's time: what's left is the CPU's share and the waiting.
class ProfilerPanel : public EditorPanel
{
public:
    ProfilerPanel(const VulkanProfiler& profiler) : EditorPanel("Profiler"), m_Profiler(profiler) { }

private:
    void DrawContents() override;

private:
    const VulkanProfiler& m_Profiler;
};

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_EDITOR_ENABLED) && defined(GRAM_VULKAN_RENDERER)
