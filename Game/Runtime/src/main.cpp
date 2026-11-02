#include "Gram/EntryPoint.hpp"
#include "Gram/Application/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Scripting/GameAPI.hpp"

#include "Greeting.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec)
    {
        GramImGuiOnly(GramCheckVoid(m_VulkanRenderer->GetImGuiApplication().RegisterLayer<RuntimeApplication::UI>(m_UILayer, *this), "couldn't initialize the UI"));
        GramCheckVoid(RegisterLayer<Gram::World>(m_World), "couldn't instantiate the Game World");
        GramCheckVoid(RegisterLayer<Gram::GameAPILoader>(m_GameAPI, *m_World, spec.m_Workspace, "GameLogic"), "couldn't load the Game API: GameLogic");
    
        const Gram::Greeting greeting = { .Text = "Hello world!" };
        m_GreetingEntity = m_World->CreateEntity();
        GramInfo("Greeting Entity: Index {}, Generation {}", m_GreetingEntity.Index, m_GreetingEntity.Generation);
        m_World->AddComponent(m_GreetingEntity, &Gram::GreetingDescriptor, &greeting);
    }

private:
    Gram::ObjectID m_GreetingEntity = {};
    std::shared_ptr<Gram::GameAPILoader> m_GameAPI;
    std::shared_ptr<Gram::World> m_World;

#ifdef GRAM_IMGUI_ENABLED
    struct UI : Gram::ImGuiApplicationLayer
    {
        UI(RuntimeApplication& app) : m_App(app) { }
        
        bool OnDrawFrame() override
        {
            if (ImGui::Begin("Inspector", &m_IsOpen, ImGuiWindowFlags_None))
            {
                if (ImGui::Button("Reload")) m_App.m_GameAPI->Reload();
                
                ImGui::BeginDisabled(m_App.m_GameAPI == nullptr);
                if (ImGui::Button("Greet")) m_App.m_GameAPI->Emit("greet", m_App.m_GreetingEntity);
                ImGui::EndDisabled();
                
                if (void* greeting = m_App.m_World->GetComponent(m_App.m_GreetingEntity, Gram::GreetingDescriptor.ID))
                    DrawComponent(Gram::GreetingDescriptor, greeting);
                else
                    ImGui::TextDisabled("no entity with a Greeting");
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
        
        RuntimeApplication& m_App;
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
