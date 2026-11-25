#include "Gram/Renderer/BenchmarkRecorder.hpp"
#include "Gram/Renderer/VulkanProfiler.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

// The local date and time, for the file's name: 2026-11-25_14-30-05.
static std::string Timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", &local);
    return text;
}

// A JSON string: quotes and backslashes escaped, control characters dropped (none are expected in a setting).
static std::string JsonString(std::string_view text)
{
    std::string out = "\"";
    for (char c : text)
    {
        if      (c == '"' || c == '\\')               { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) >= 32) { out += c; }
    }
    return out + "\"";
}

void BenchmarkRecorder::WriteSettings(const std::filesystem::path& path, const Settings& settings)
{
    std::ofstream json(path);
    json << "{\n";
    for (size_t i = 0; i < settings.size(); i++)
    {
        json << "    " << JsonString(settings[i].Key) << ": " << JsonString(settings[i].Value) << (i + 1 < settings.size() ? ",\n" : "\n");
    }
    json << "}\n";
}

bool BenchmarkRecorder::Start(const std::filesystem::path& folder, std::vector<std::string> passes, const Settings& settings, float intervalSeconds, float durationSeconds)
{
    Stop();

    std::error_code error;
    std::filesystem::create_directories(folder, error);
    GramCheck(!error, "benchmark: couldn't create {}: {}", folder.string(), error.message());

    const std::string name = Timestamp();
    m_Path = folder / (name + ".csv");
    m_File.open(m_Path);
    GramCheck(m_File.is_open(), "benchmark: couldn't write {}", m_Path.string());
    WriteSettings(folder / (name + ".json"), settings);

    m_Passes   = std::move(passes);
    m_Interval = std::max(intervalSeconds, 0.01f);
    m_Duration = durationSeconds;
    m_Elapsed  = 0.0f;

    m_IntervalStart   = 0.0f;
    m_IntervalFrames  = 0;
    m_IntervalFrameMs = 0.0;
    m_IntervalSlowest = 0.0f;
    m_IntervalPassMs    .assign(m_Passes.size(), 0.0);
    m_IntervalPassFrames.assign(m_Passes.size(), 0);

    m_File << "time_s";
    for (const std::string& pass : m_Passes) m_File << ',' << pass << " ms";
    m_File << ",frame_ms,fps,slowest_frame_ms\n";

    GramInfo("benchmark: recording {} s into {}", m_Duration, m_Path.string());
    return true;
}

void BenchmarkRecorder::Stop()
{
    if (!IsRecording()) return;
    if (m_IntervalFrames > 0) WriteRow();   // the last, shorter interval
    m_File.close();
    GramInfo("benchmark: {:.1f} s recorded in {}", m_Elapsed, m_Path.string());
}

//  1. m_Elapsed grows by this frame's time (frameMs is in ms, m_Elapsed in s).
//  2. The interval's frame count, its sum of frame times and its slowest frame take this frame in.
//  3. For each of m_Passes, find its timing by name in `timings` (a pass may be missing from a frame: the profiler
//     had nothing to read back yet, or it was full): add its ms and count the frame for that pass only.
//  4. When the interval is complete (m_Elapsed - m_IntervalStart >= m_Interval), WriteRow().
//  5. When m_Elapsed reaches m_Duration, Stop().
void BenchmarkRecorder::AddFrame(std::span<const VulkanProfiler::Timing> timings, float frameMs)
{
    m_Elapsed         += frameMs / 1000.0f;
    m_IntervalFrameMs += frameMs;
    m_IntervalFrames  += 1;
    m_IntervalSlowest  = std::max(m_IntervalSlowest, frameMs);
    
    for (uint32_t i = 0; i < m_Passes.size(); i++)
    {
        const std::string& passName = m_Passes[i];
        auto found = std::find_if(timings.begin(), timings.end(), [&passName](const VulkanProfiler::Timing& timing)
        {
            return timing.Name == passName;
        });

        if (found != timings.end())
        {
            m_IntervalPassMs[i]     += found->Ms;
            m_IntervalPassFrames[i] += 1;
        }
    }

    if (m_Elapsed - m_IntervalStart >= m_Interval) WriteRow();
    if (m_Elapsed >= m_Duration) Stop();
}

//  - time_s: m_IntervalStart (where the interval began)
//  - one column per pass: its mean GPU ms over the frames it was measured in (empty when it never was)
//  - frame_ms: the mean frame time; fps: frames / the interval's real length in seconds (not 1000 / frame_ms:
//    the same thing only when every frame lasts as long); slowest_frame_ms
//  Then the next interval starts at m_Elapsed, with every sum and count back to 0.
void BenchmarkRecorder::WriteRow()
{
    m_File << m_IntervalStart;

    for (uint32_t i = 0; i < m_Passes.size(); i++)
    {
        m_File << ",";
        if (m_IntervalPassFrames[i] > 0.001) m_File << m_IntervalPassMs[i] / m_IntervalPassFrames[i];
    }

    m_File << "," << m_IntervalFrameMs / m_IntervalFrames
           << "," << m_IntervalFrames  / (m_Elapsed - m_IntervalStart)
           << "," << m_IntervalSlowest << "\n";

    m_IntervalStart   = m_Elapsed;
    m_IntervalFrameMs = 0.0;
    m_IntervalFrames  = 0;
    m_IntervalSlowest = 0;
    m_IntervalPassMs    .assign(m_Passes.size(), 0.0);
    m_IntervalPassFrames.assign(m_Passes.size(), 0);
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
