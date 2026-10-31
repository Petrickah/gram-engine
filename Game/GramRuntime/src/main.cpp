#include "Gram/EntryPoint.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#include "Greeting.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec)
    {
        m_GreetingComponent = { .Text = "Hello, world!" };
        static const Gram::HostAPI host { .Log = [](const char* message) { GramInfo(message); } };
        GramCheckVoid(Gram::gram_core_init(&host), "gram-core init failed");

        GramImGuiOnly(GramCheckVoid(m_VulkanRenderer->GetImGuiApplication().RegisterLayer<RuntimeApplication::UI>(m_UILayer, *this), "couldn't initialize the UI"));
    }

private:
    Gram::Greeting m_GreetingComponent;

#ifdef GRAM_IMGUI_ENABLED
    struct UI : Gram::ImGuiApplicationLayer
    {
        RuntimeApplication& m_App;

        UI(RuntimeApplication& app) : m_App(app) { }
        
        bool OnDrawFrame() override
        {
            if (ImGui::Begin("Inspector", &m_IsOpen, ImGuiWindowFlags_None))
            {
                if (ImGui::Button("Greet")) gram_core_greet(&m_App.m_GreetingComponent);
                DrawComponent(Gram::GreetingDescriptor, &m_App.m_GreetingComponent);
            }
            
            ImGui::End();

            return m_IsOpen;
        }

        void DrawComponent(const Gram::ComponentDescriptor& desc, void* data)
        {
            if (!ImGui::CollapsingHeader(desc.Name, ImGuiTreeNodeFlags_DefaultOpen)) return;
            for (const Gram::FieldDescriptor& field : std::span<const Gram::FieldDescriptor>(desc.Fields, desc.Count))
            {
                void* value = static_cast<std::byte*>(data) + field.Offset;
                switch (field.Type)
                {
                    case Gram::FieldDescriptor::FieldType::Str: ImGui::InputText(field.Name, static_cast<char*>(value), field.Size); break;
                    default: ImGui::TextDisabled("%s: unsupported type", field.Name); break;
                    // case FieldType::F32:   ImGui::DragFloat(field.Name, static_cast<float*>(value)); break;
                    // case FieldType::Vec3:  ImGui::DragFloat3(field.Name, static_cast<float*>(value)); break;
                    // un case pentru fiecare tip pe care îl acceptă schema
                }
            }
        }

    private:
        bool m_IsOpen = true;
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
