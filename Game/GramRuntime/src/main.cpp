#include "Gram/EntryPoint.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec)
    {
        GramImGuiOnly(GramCheckVoid(m_VulkanRenderer->GetImGuiApplication().RegisterLayer<RuntimeApplication::UI>(m_UILayer, *this), "couldn't initialize the UI"));
    }

private:
#ifdef GRAM_IMGUI_ENABLED
    struct UI : Gram::ImGuiApplicationLayer
    {
        RuntimeApplication& m_App;
        UI(RuntimeApplication& app) : m_App(app) { }
        
        bool OnDrawFrame() override
        {
            ImGui::ShowDemoWindow();
            return true;
        }
    };
    
    std::shared_ptr<UI> m_UILayer = nullptr;
#endif // #ifdef GRAM_IMGUI_ENABLED
};

std::shared_ptr<Gram::Application> Gram::CreateApplication(int argc, char** argv)
{
    const bool  enableLogging  = true;
    const char* executableFile = argc > 0 ? argv[0] : nullptr;
    
    // Call into the logger entry point before handing off the application
    GramLoggerOnly(Gram::LoggerEntryPoint(enableLogging));
    
    return std::make_shared<RuntimeApplication>(
        Gram::GLFWApplicationSpecification {
            .m_EnableLogging = enableLogging,
            .m_WindowSize    = glm::vec2(1280, 720),
            .m_WindowTitle   = "Gram Application with Vulkan",
            .m_ShaderProgram = "Default",
            .m_Workspace     = Gram::GetExecutableDirectory(executableFile)
        }
    );
}
