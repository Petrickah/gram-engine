#pragma once

#include "Gram/Editor/EditorPanel.hpp"

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

class GameAPILoader;

// The systems the loaded game registered (GameAPILoader's registry), and its Reload button.
class SystemsPanel : public EditorPanel
{
public:
    SystemsPanel(GameAPILoader& loader) : EditorPanel("Systems"), m_Loader(loader) { }

private:
    void DrawContents() override;

private:
    GameAPILoader& m_Loader;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
