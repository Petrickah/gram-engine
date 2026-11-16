#pragma once

#ifdef GRAM_EDITOR_ENABLED
GRAM_NAMESPACE_BEGIN

class GameAPILoader;

// The strip under the main menu bar with Play, Pause and Stop (GameAPILoader's ReplayState): Play
// rebuilds the game's library and runs its systems, Pause holds them, Stop puts the World back to how
// it was at Play. Not a panel: it can't be closed. Registered right after the menu bar, so it sits
// below it, and the dockspace makes room for both.
class EditorToolbar : public ImGuiApplicationLayer
{
public:
    explicit EditorToolbar(GameAPILoader& loader) : m_Loader(loader) { }

private:
    bool OnDrawFrame() override;

private:
    GameAPILoader& m_Loader;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_EDITOR_ENABLED
