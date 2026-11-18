#pragma once

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

// A dockable editor window its owner's menu ("Engine", "Game") can close and reopen. Subclasses only
// draw what's inside it. The path places it in that menu: "Debug/Ray Tracer" is the item "Ray Tracer"
// in a "Debug" submenu; its last segment is the window's title, the whole path its ImGui ID, so two
// panels with the same title in different submenus are still two windows.
class EditorPanel : public ImGuiApplicationLayer
{
public:
    EditorPanel(std::string_view path) : m_Path(path)
    {
        for (size_t start = 0; start <= m_Path.size(); )
        {
            const size_t slash = std::min(m_Path.find('/', start), m_Path.size());
            m_Segments.emplace_back(m_Path.substr(start, slash - start));
            start = slash + 1;
        }

        m_WindowID = std::format("{}###{}", m_Segments.back(), m_Path);
    }

    const std::string&              GetPath()     const { return m_Path;            }
    const std::string&              GetTitle()    const { return m_Segments.back(); }
    const std::vector<std::string>& GetSegments() const { return m_Segments;        }
    
    bool& IsOpen() { return m_IsOpen; }   // what the menu item toggles

protected:
    virtual void DrawContents() = 0;

public:
    // Always true: an ImGui layer returning false skips the layers after it, so a closed panel
    // just stops drawing. Public for EditorFunctionPanels, which draws panels that aren't layers.
    bool OnDrawFrame() final
    {
        if (!m_IsOpen) return true;

        if (ImGui::Begin(m_WindowID.c_str(), &m_IsOpen, ImGuiWindowFlags_None)) DrawContents();
        ImGui::End();
        return true;
    }

private:
    std::string              m_Path     = "";
    std::string              m_WindowID = "";
    std::vector<std::string> m_Segments = {};
    bool                     m_IsOpen   = true;
};

// A panel without a subclass of its own: a function draws its contents (Editor::AddPanel(path, draw)).
// Not a layer: EditorFunctionPanels holds and draws them all.
class FunctionPanel : public EditorPanel
{
public:
    using Func = std::function<void()>;
    FunctionPanel(std::string_view path, Func callback): EditorPanel(path), m_Callback(std::move(callback)) { }

private:
    void DrawContents() final { if (m_Callback) m_Callback(); }

private:
    Func m_Callback;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
