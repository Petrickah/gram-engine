#pragma once

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

class GameAPILoader;

// The systems the loaded game registered (GameAPILoader's registry), and its Reload button.
class SystemsPanel : public ImGuiApplicationLayer
{
public:
    SystemsPanel(GameAPILoader& loader) : m_Loader(loader) { }

private:
    virtual bool OnDrawFrame() override;

private:
    GameAPILoader& m_Loader;
    bool           m_IsOpen = true;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
