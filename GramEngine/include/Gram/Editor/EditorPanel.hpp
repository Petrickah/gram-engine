#pragma once

#ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_BEGIN

// A dockable editor window the Window menu can close and reopen. Subclasses only draw what's
// inside it.
class EditorPanel : public ImGuiApplicationLayer
{
public:
    EditorPanel(const char* name) : m_Name(name) { }

    const char* GetName() const { return m_Name; }
    bool&       IsOpen()        { return m_IsOpen; }   // what the Window menu toggles

protected:
    virtual void DrawContents() = 0;

private:
    // Always true: an ImGui layer returning false skips the layers after it, so a closed panel
    // just stops drawing.
    bool OnDrawFrame() final
    {
        if (!m_IsOpen) return true;

        if (ImGui::Begin(m_Name, &m_IsOpen, ImGuiWindowFlags_None)) DrawContents();
        ImGui::End();
        return true;
    }

private:
    const char* m_Name;
    bool        m_IsOpen = true;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED
