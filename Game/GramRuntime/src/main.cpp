#include "Gram/EntryPoint.hpp"
#include "Gram/Application/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"

#include "Gram/Scripting/HostAPI.hpp"

#include "GameAPI.hpp"
#include "Greeting.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    ~RuntimeApplication()
    {
        if (m_Game)
        {
            m_Game->OnUnload();
            m_Game = nullptr;
        }

        m_GameLibrary.reset();
    }
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec)
    {
        GramImGuiOnly(GramCheckVoid(m_VulkanRenderer->GetImGuiApplication().RegisterLayer<RuntimeApplication::UI>(m_UILayer, *this), "couldn't initialize the UI"));

        m_GameLibrary = std::make_unique<Gram::DynamicLibrary>(spec.m_Workspace / Gram::GetDynamicLibraryFileName("game_logic"));
        GramCheckVoid(m_GameLibrary->IsValid(), "error loading the game logic library");
        
        if (const Gram::GameAPI* gameAPI = m_GameLibrary->FindSymbol<Gram::GetGameAPIFunction>("game_get_api")())
        {
            GramCheckVoid(gameAPI->Version == Gram::GameAPIVersion, "no valid game api: version mismatch");
            m_Game = gameAPI->OnLoad(&Gram::g_Host);
        }

        m_GreetingComponent = { .Text = "Hello, world!" };
    }

private:
    Gram::Greeting m_GreetingComponent;
    std::unique_ptr<Gram::DynamicLibrary> m_GameLibrary;
    const Gram::GameAPI*                  m_Game = nullptr;

#ifdef GRAM_IMGUI_ENABLED
    struct UI : Gram::ImGuiApplicationLayer
    {
        RuntimeApplication& m_App;

        UI(RuntimeApplication& app) : m_App(app) { }
        
        bool OnDrawFrame() override
        {
            if (ImGui::Begin("Inspector", &m_IsOpen, ImGuiWindowFlags_None))
            {
                ImGui::BeginDisabled(m_App.m_Game == nullptr);
                if (ImGui::Button("Greet")) m_App.m_Game->Greet(&m_App.m_GreetingComponent);
                ImGui::EndDisabled();
                //if (ImGui::Button("Greet")) gram_core_greet(&m_App.m_GreetingComponent);
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
