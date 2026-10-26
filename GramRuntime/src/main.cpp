#include "Gram/EntryPoint.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#ifdef GRAM_VULKAN_RENDERER
class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const std::string& workspace, const Gram::GLFWApplicationSpecification& spec)
        : GLFWApplication(spec)
    {
        constexpr bool displayDebug = true;
        Gram::VulkanRendererSpecification rendererSpec(this, workspace, spec, displayDebug);

        std::shared_ptr<Gram::VulkanRenderer> renderer = nullptr;
        GramCheckVoid(RegisterLayer<Gram::VulkanRenderer>(renderer, &rendererSpec), "couldn't initialize the Vulkan Renderer framework");
        GramImGuiOnly(GramCheckVoid(renderer->GetImGuiApplication().RegisterLayer<RuntimeApplication::UI>(m_UILayer, *this), "couldn't initialize the UI"));
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
#endif // #ifdef GRAM_VULKAN_RENDERER

std::shared_ptr<Gram::Application> Gram::CreateApplication(int argc, char** argv)
{
    const bool enableLogging   = true;
    const char* executableFile = argc > 0 ? argv[0] : nullptr;

    // Call into the logger entry point before handing off the application
    GramLoggerOnly(Gram::LoggerEntryPoint(enableLogging));

    return std::make_shared<RuntimeApplication>(
        Gram::GetExecutableDirectory(executableFile),
        Gram::GLFWApplicationSpecification {
            .m_EnableLogging = enableLogging,
            .m_WindowSize    = glm::vec2(1280, 720),
            .m_WindowTitle   = "Gram Application with Vulkan",
        }
    );
}
