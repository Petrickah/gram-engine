#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_LOGGER_ENABLED)
GRAM_NAMESPACE_BEGIN

// The last messages of every logger (the engine's, Vulkan's and the game's), read back from the
// editor's ring buffer sink. A message with details (a list logged after it, or more lines)
// collapses them under itself. Clear and the filters only change what this panel shows: the
// messages stay in the buffer.
class LoggerPanel : public EditorPanel
{
public:
    LoggerPanel() : EditorPanel("Log") { m_ShowLevel.fill(true); }

private:
    struct Detail
    {
        spdlog::level::level_enum Level;
        std::string Text;
    };

    struct Entry
    {
        int64_t Key;                          // the message's time: which entry is expanded
        spdlog::level::level_enum Level;
        std::string Text;
        std::vector<Detail> Details;
        bool IsList = false;                  // its details are a list logger's items
        std::string Group;                    // a system's logger: its next messages join this entry
    };

    struct Row                                // one line on screen
    {
        const Entry* Owner;
        const Detail* Line;                   // nullptr: the entry's own line
    };

    void DrawContents() override;
    void DrawToolbar();
    void DrawRow(const Row& row);
    std::vector<Entry> CollectEntries() const;
    std::vector<Row> CollectRows(const std::vector<Entry>& entries) const;

private:
    spdlog::log_clock::time_point m_ClearedAt {};             // epoch: nothing hidden yet
    std::array<bool, spdlog::level::n_levels> m_ShowLevel {};  // indexed by spdlog::level
    ImGuiTextFilter m_Filter;
    std::set<int64_t> m_Expanded;                             // keys of the expanded entries
};

GRAM_NAMESPACE_END
#endif // #if defined(GRAM_IMGUI_ENABLED) && defined(GRAM_LOGGER_ENABLED)
