#pragma once

#include "Gram/Renderer/VulkanDevice.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanProfiler : private UniqueObject
{
public:
    struct Timing { std::string Name; float Ms = 0.0f; };

    // One query pool per frame in flight, 2 timestamps (start, end) per scope, at most MaxScopes scopes.
    static constexpr uint32_t MaxScopes = 32;
    bool Create(VulkanDevice& device);   // false when the queue can't write timestamps

    // At the start of a frame's recording, after VulkanFrame::Begin waited for this slot's fence: reads the
    // times this slot recorded last time (its GPU work is done), then resets its pool in `cmd`.
    void BeginFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

    // Around one pass's Record: a timestamp before, one after.
    void Begin(vk::CommandBuffer cmd, const char* name);
    void End  (vk::CommandBuffer cmd);

    const std::vector<Timing>& GetTimings() const { return m_Timings; }   // the last frame read back, smoothed

private:
    void Smooth(const std::string& name, float ms);

    vk::Device m_Device = {};
    float      m_Period = 0.0f;   // ns per tick
    bool       m_Valid  = false;  // Create succeeded: timestamps are supported

    // One pool per frame in flight, and the names of the scopes it recorded the last time that slot ran:
    // names[i] goes with queries 2i (start) and 2i + 1 (end).
    std::array<vk::UniqueQueryPool, vkMaxFramesInFlight>      m_Pools;
    std::array<std::vector<std::string>, vkMaxFramesInFlight> m_Names;

    uint32_t m_Frame = 0;   // the slot being recorded now
    uint32_t m_Query = 0;   // the next free query in it

    std::vector<Timing> m_Timings;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER