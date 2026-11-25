#pragma once

#include "Gram/Renderer/VulkanProfiler.hpp"

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

// A benchmark recording (docs/wave-2-benchmarking.md, section 5): the profiler's raw GPU times and the frame's
// time, gathered over fixed intervals, one CSV row per interval, for a fixed duration. The settings that ran
// go into a JSON file next to it, with the same name. Started and stopped from the Profiler panel, fed once
// per frame by the renderer.
//
// The CSV: a header, then one row per interval:
//   time_s, <pass> ms (one column per pass, in the order they ran when the recording started), frame_ms, fps, slowest_frame_ms
class BenchmarkRecorder
{
public:
    struct Setting { std::string Key; std::string Value; };
    using Settings = std::vector<Setting>;   // in the order they were added: the JSON keeps it

    // Opens <folder>/<date>_<time>.csv (and .json, from `settings`), writes the header. `passes` are the
    // columns, fixed for the whole recording: a pass that appears later isn't recorded.
    bool Start(const std::filesystem::path& folder, std::vector<std::string> passes, const Settings& settings, float intervalSeconds, float durationSeconds);
    // Writes the interval gathered so far, if it has any frame, and closes the file.
    void Stop();

    // Once per frame, by the renderer, while recording: the last frame's GPU time per pass (the profiler's raw
    // values, not the panel's smoothed ones) and the frame's own time.
    void AddFrame(std::span<const VulkanProfiler::Timing> timings, float frameMs);

    bool                         IsRecording() const { return m_File.is_open(); }
    float                        GetElapsed()  const { return m_Elapsed;  }   // seconds since Start
    float                        GetDuration() const { return m_Duration; }
    const std::filesystem::path& GetPath()     const { return m_Path;     }   // the CSV, also once stopped

private:
    // One CSV row for the interval just gathered, then that interval emptied for the next one.
    void WriteRow();

    static void WriteSettings(const std::filesystem::path& path, const Settings& settings);

private:
    std::ofstream            m_File;
    std::filesystem::path    m_Path;
    std::vector<std::string> m_Passes;           // the columns, by name
    float                    m_Interval = 1.0f;  // seconds per row
    float                    m_Duration = 0.0f;  // seconds; reached, the recording stops itself
    float                    m_Elapsed  = 0.0f;  // seconds since Start: the frames' times added up

    // The interval being gathered: AddFrame adds to it, WriteRow turns it into a row and empties it.
    float                 m_IntervalStart   = 0.0f;   // m_Elapsed when it began: the row's time_s
    uint32_t              m_IntervalFrames  = 0;      // frames in it
    double                m_IntervalFrameMs = 0.0;    // their times added up
    float                 m_IntervalSlowest = 0.0f;   // the longest of them
    std::vector<double>   m_IntervalPassMs;           // per m_Passes[i]: its GPU ms added up
    std::vector<uint32_t> m_IntervalPassFrames;       // per m_Passes[i]: the frames it was measured in
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
