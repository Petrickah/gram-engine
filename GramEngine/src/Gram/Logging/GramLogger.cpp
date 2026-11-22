#include "Gram/Common.hpp"
#include "Gram/Logging/GramLogger.hpp"

#ifdef GRAM_LOGGER_ENABLED
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>

GRAM_NAMESPACE_BEGIN

// One logger per subsystem, all writing to the same console sink. The level filters what
// gets printed (trace < debug < info < warn < error), so a single logger handles every level.
static std::shared_ptr<spdlog::logger> s_GramLogger;
static std::shared_ptr<spdlog::logger> s_EditorLogger;
static std::shared_ptr<spdlog::logger> s_VulkanLogger;
static std::shared_ptr<spdlog::logger> s_VulkanListLogger;

static std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> s_EditorSink;

// What GetSystemLogger needs to make a logger like the others, later, on demand.
static spdlog::sink_ptr s_ConsoleSink;
static spdlog::level::level_enum s_Level = spdlog::level::off;
static std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>> s_SystemLoggers;

// Between a logger and its sinks: a warning or an error seen again within a second of its last time (a check
// failing every frame, a validation message about every frame's commands) is shown once, not 60 times a second,
// even with other messages in between. When it stops for a second, how many times it was hidden is shown, at
// the next message. Only warnings and errors: an info message can say the same thing twice on purpose (Greet).
class RepeatFilterSink final : public spdlog::sinks::dist_sink<std::mutex>
{
public:
    explicit RepeatFilterSink(std::vector<spdlog::sink_ptr> sinks) : dist_sink(std::move(sinks)) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        constexpr auto runGap = std::chrono::seconds(1);

        // The runs that ended: a line saying how many times they were hidden, then they're forgotten.
        for (auto it = m_Runs.begin(); it != m_Runs.end(); )
        {
            if (msg.time - it->second.LastSeen < runGap) { ++it; continue; }
            if (it->second.Hidden > 0)
            {
                const std::string text = std::format("(\"{}\" repeated {} more times)", it->first, it->second.Hidden);
                dist_sink::sink_it_(spdlog::details::log_msg(msg.time, {}, msg.logger_name, it->second.Level, text));
            }
            it = m_Runs.erase(it);
        }

        if (msg.level >= spdlog::level::warn)
        {
            const std::string payload(msg.payload.data(), msg.payload.size());
            auto [run, first] = m_Runs.try_emplace(payload, Run { .Level = msg.level, .LastSeen = msg.time });
            if (!first)
            {
                run->second.LastSeen = msg.time;
                run->second.Hidden++;
                return;
            }
        }

        dist_sink::sink_it_(msg);
    }

private:
    struct Run
    {
        spdlog::level::level_enum     Level;
        spdlog::log_clock::time_point LastSeen;
        uint64_t                      Hidden = 0;
    };
    std::map<std::string, Run, std::less<>> m_Runs;   // by message text: the warnings and errors seen in the last second
};

static spdlog::sink_ptr FilterRepeats(const spdlog::sinks_init_list& sinks)
{
    return std::make_shared<RepeatFilterSink>(std::vector<spdlog::sink_ptr>(sinks));
}

static std::shared_ptr<spdlog::logger> CreateLogger(const std::string& name, const spdlog::sinks_init_list& sinks, spdlog::level::level_enum level)
{
    std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(name, FilterRepeats(sinks));
    logger->set_level(level);
    // [14:03:12.345] [error] [Vulkan] message; %^...%$ colors the level by severity.
    logger->set_pattern("[%T.%e] [%^%l%$] [%n] %v");
    // Errors are flushed right away, so they still show up if the process crashes next.
    logger->flush_on(spdlog::level::err);

    return logger;
}

// Custom pattern flag (%* in the list pattern): replaced by the item's marker, picked by level.
// spdlog calls format() for every message, with the message itself (and so its level) in msg.
class ListMarkerFormatter : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        const std::string_view marker = GetMarker(msg.level);
        dest.append(marker.data(), marker.data() + marker.size());
    }

    // The formatter gets copied into every sink, so spdlog needs a way to copy this flag too.
    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return std::make_unique<ListMarkerFormatter>();
    }

private:
    static std::string_view GetMarker(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace: return "-"; // available, not chosen
        case spdlog::level::debug: return "+"; // added automatically, without being requested
        case spdlog::level::info:  return "X"; // chosen / requested
        default:                   return "!"; // warn and error: requested but missing
        }
    }
};

static std::shared_ptr<spdlog::logger> CreateListLogger(const std::string& name, const spdlog::sinks_init_list& sinks, spdlog::level::level_enum level)
{
    std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(name, FilterRepeats(sinks));
    logger->set_level(level);
    // "     - [X] message"; %^...%$ colors the marker by severity.
    std::unique_ptr<spdlog::pattern_formatter> formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<ListMarkerFormatter>('*').set_pattern("     - [%^%*%$] %v");
    logger->set_formatter(std::move(formatter));
    // Errors are flushed right away, so they still show up if the process crashes next.
    logger->flush_on(spdlog::level::err);

    return logger;
}

void LoggerEntryPoint(bool loggingEnabled)
{
    try
    {
        // The pattern lives on the sink, not the logger: set_pattern on a logger overwrites it on
        // every sink that logger writes to. So the list logger, with its own pattern, needs its
        // own sink, otherwise the Gram/Vulkan loggers would print list markers too.
        spdlog::sink_ptr consoleSink  = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        spdlog::sink_ptr listSink     = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                         s_EditorSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1000);
        const spdlog::level::level_enum level = loggingEnabled ? spdlog::level::trace : spdlog::level::off;

        s_GramLogger       = CreateLogger("Gram",   {consoleSink, s_EditorSink}, level);
        s_EditorLogger     = CreateLogger("Editor", {consoleSink, s_EditorSink}, level);
        s_VulkanLogger     = CreateLogger("Vulkan", {consoleSink, s_EditorSink}, level);
        s_VulkanListLogger = CreateListLogger(std::string("Vulkan").append(ListLoggerSuffix), {listSink, s_EditorSink}, level);

        s_ConsoleSink = consoleSink;
        s_Level       = level;
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        std::cerr << "[Gram] logger initialization failed: " << ex.what() << '\n';
    }
}

spdlog::logger* GetGramLogger()
{
    return s_GramLogger.get();
}

spdlog::logger *GetEditorLogger()
{
    return s_EditorLogger.get();
}

spdlog::logger* GetVulkanLogger()
{
    return s_VulkanLogger.get();
}

spdlog::logger* GetVulkanListLogger()
{
    return s_VulkanListLogger.get();
}

spdlog::logger* GetSystemLogger(std::string_view system)
{
    if (!s_ConsoleSink) return nullptr;

    auto found = s_SystemLoggers.find(system);
    if (found == s_SystemLoggers.end())
    {
        const std::string name = std::string(SystemLoggerPrefix).append(system);
        found = s_SystemLoggers.emplace(system, CreateLogger(name, {s_ConsoleSink, s_EditorSink}, s_Level)).first;
    }
    return found->second.get();
}

std::vector<spdlog::details::log_msg_buffer> GetEditorMessages()
{
    return s_EditorSink ? s_EditorSink->last_raw() : std::vector<spdlog::details::log_msg_buffer> {};
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_LOGGER_ENABLED
