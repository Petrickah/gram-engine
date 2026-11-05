#include "Gram/Scripting/Common.hpp"
#include "Gram/Editor/LoggerPanel.hpp"
#include "Gram/Logging/GramLogger.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_LOGGER_ENABLED)
GRAM_NAMESPACE_BEGIN

namespace
{
    // The levels the toolbar offers; debug and critical aren't used by Gram's macros.
    constexpr std::array<std::pair<spdlog::level::level_enum, const char*>, 4> ShownLevels {{
        { spdlog::level::trace, "Trace"   },
        { spdlog::level::info,  "Info"    },
        { spdlog::level::warn,  "Warning" },
        { spdlog::level::err,   "Error"   },
    }};

    std::string_view View(const spdlog::memory_buf_t& buffer) { return { buffer.data(), buffer.size() }; }
    std::string_view View(spdlog::string_view_t text)         { return { text.data(), text.size() }; }

    bool IsListItem(const spdlog::details::log_msg_buffer& msg) { return View(msg.logger_name).ends_with(ListLoggerSuffix); }

    // A list item's level only picks its marker (see GramVulkanList*), it isn't a severity.
    const char* ListMarker(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace: return "[-]";
        case spdlog::level::debug: return "[+]";
        case spdlog::level::info:  return "[X]";
        default:                   return "[!]";
        }
    }

    ImVec4 LevelColor(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        case spdlog::level::warn:     return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        case spdlog::level::err:
        case spdlog::level::critical: return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        default:                      return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    bool Passes(const ImGuiTextFilter& filter, std::string_view text)
    {
        return filter.PassFilter(text.data(), text.data() + text.size());
    }
}

void LoggerPanel::DrawToolbar()
{
    if (ImGui::Button("Clear")) m_ClearedAt = spdlog::log_clock::now();

    for (const auto& [level, label] : ShownLevels)
    {
        ImGui::SameLine();
        ImGui::Checkbox(label, &m_ShowLevel[level]);
    }

    ImGui::SameLine();
    m_Filter.Draw("Filter (inc,-exc)", 180.0f);
}

// One entry per message, plus its details: the list items logged right after it (a list
// logger's messages) and the lines after the first one of a multi-line message.
std::vector<LoggerPanel::Entry> LoggerPanel::CollectEntries() const
{
    std::vector<Entry> entries;
    for (const spdlog::details::log_msg_buffer& msg : GetEditorMessages())
    {
        if (msg.time <= m_ClearedAt) continue;

        if (IsListItem(msg))
        {
            // A list starts with a regular message ("available extensions:"); a list without
            // one gets a header of its own, named after its logger.
            if (entries.empty() || !entries.back().IsList)
            {
                const std::string_view name = View(msg.logger_name);
                const bool introduced = !entries.empty() && entries.back().Details.empty() && entries.back().Text.ends_with(':');
                if (!introduced)
                    entries.push_back({ .Key = msg.time.time_since_epoch().count(), .Level = spdlog::level::info,
                                        .Text = std::format("[{}] list", name.substr(0, name.size() - ListLoggerSuffix.size())) });
                entries.back().IsList = true;
            }
            entries.back().Details.push_back({ msg.level, std::format("{} {}", ListMarker(msg.level), View(msg.payload)) });
            continue;
        }

        const std::string_view payload = View(msg.payload);
        const size_t newline = payload.find('\n');

        // A game system's messages in a row stay under its first one, until another logger
        // logs in between: tick's once-a-second line doesn't bury everything else.
        const std::string_view logger = View(msg.logger_name);
        if (logger.starts_with(SystemLoggerPrefix) && !entries.empty() && entries.back().Group == logger)
        {
            Entry& group = entries.back();
            group.Level = std::max(group.Level, msg.level);   // a warning inside stays visible
            for (size_t start = 0; start != std::string_view::npos; )
            {
                const size_t end = payload.find('\n', start);
                group.Details.push_back({ msg.level, std::string(payload.substr(start, end - start)) });
                start = end == std::string_view::npos ? end : end + 1;
            }
            continue;
        }

        Entry entry {
            .Key   = msg.time.time_since_epoch().count(),
            .Level = msg.level,
            .Text  = std::format("[{}] {}", logger, payload.substr(0, newline)),
            .Group = logger.starts_with(SystemLoggerPrefix) ? std::string(logger) : std::string(),
        };
        // The rest of a multi-line message, one detail per line, in the message's own level.
        for (size_t start = newline; start != std::string_view::npos; )
        {
            const size_t end = payload.find('\n', start + 1);
            entry.Details.push_back({ msg.level, std::string(payload.substr(start + 1, end - start - 1)) });
            start = end;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

// The rows on screen, in order: every visible entry, followed by its details while expanded.
// Flat, so the clipper can skip what's off screen; whether an entry is expanded is kept here,
// by message, because the clipper needs to know it before anything is drawn.
std::vector<LoggerPanel::Row> LoggerPanel::CollectRows(const std::vector<Entry>& entries) const
{
    std::vector<Row> rows;
    for (const Entry& entry : entries)
    {
        if (!m_ShowLevel[entry.Level]) continue;

        // An entry is shown when it or one of its details matches: searching for an extension
        // finds the list it's in.
        const bool headerPasses = Passes(m_Filter, entry.Text);
        const bool anyDetailPasses = std::ranges::any_of(entry.Details, [&](const Detail& d) { return Passes(m_Filter, d.Text); });
        if (!headerPasses && !anyDetailPasses) continue;

        rows.push_back({ &entry, nullptr });
        if (!m_Expanded.contains(entry.Key)) continue;

        for (const Detail& detail : entry.Details)
            if (headerPasses || Passes(m_Filter, detail.Text))
                rows.push_back({ &entry, &detail });
    }
    return rows;
}

void LoggerPanel::DrawRow(const Row& row)
{
    if (row.Line)
    {
        ImGui::Indent();
        ImGui::TextColored(LevelColor(row.Line->Level), "%s", row.Line->Text.c_str());
        ImGui::Unindent();
        return;
    }

    const Entry& entry = *row.Owner;
    if (entry.Details.empty())
    {
        // Aligned with the headers' text, past where their arrow is.
        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
        ImGui::TextColored(LevelColor(entry.Level), "%s", entry.Text.c_str());
        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        return;
    }

    // A tree node that draws no children itself (NoTreePushOnOpen): the details are rows of
    // their own, so the clipper can skip them too. Its open state is m_Expanded's.
    ImGui::PushID(&entry);
    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.Level));
    ImGui::SetNextItemOpen(m_Expanded.contains(entry.Key));
    const bool open = ImGui::TreeNodeEx("##entry", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                                        "%s (%zu)", entry.Text.c_str(), entry.Details.size());
    ImGui::PopStyleColor();
    ImGui::PopID();

    if (open) m_Expanded.insert(entry.Key);
    else      m_Expanded.erase(entry.Key);
}

void LoggerPanel::DrawContents()
{
    DrawToolbar();
    ImGui::Separator();

    const std::vector<Entry> entries = CollectEntries();
    const std::vector<Row> rows = CollectRows(entries);

    if (ImGui::BeginChild("scroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        // Only the rows on screen are drawn, however many there are.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                DrawRow(rows[i]);

        // Follow new messages only while already at the bottom: scrolling up to read stays put.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_LOGGER_ENABLED)
