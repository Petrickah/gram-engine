#include "Gram/EntryPoint.hpp"
#include "Gram/Application/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/InspectorPanel.hpp"
#include "Gram/Editor/SystemsPanel.hpp"

#include "Components.generated.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec)
    {
        GramCheckVoid(RegisterLayer<Gram::World>(m_World), "couldn't instantiate the Game World");
        GramCheckVoid(RegisterLayer<Gram::GameAPILoader>(m_GameAPI, *m_World, spec.m_Workspace, "GameLogic"), "couldn't load the Game API: GameLogic");
    
        const Gram::Greeting greeting = { .Text = "Hello world!" };
        m_GreetingEntity = m_World->CreateEntity();
        GramInfo("Greeting Entity: Index {}, Generation {}", m_GreetingEntity.Index, m_GreetingEntity.Generation);
        m_World->AddComponent(m_GreetingEntity, &Gram::GreetingDescriptor, &greeting);

        // After the World and the loader: the panels keep references to them.
        GramImGuiOnly(RegisterPanels());
    }

private:
    Gram::ObjectID m_GreetingEntity = {};
    std::shared_ptr<Gram::GameAPILoader> m_GameAPI;
    std::shared_ptr<Gram::World> m_World;

#ifdef GRAM_IMGUI_ENABLED
    // The game's own window: what only this game has (the Greet button).
    struct GameUI : Gram::ImGuiApplicationLayer
    {
        GameUI(RuntimeApplication& app) : m_App(app) { }
        
        bool OnDrawFrame() override
        {
            if (ImGui::Begin("Game"))
            {
                ImGui::BeginDisabled(m_App.m_GameAPI->GetGameAPI() == nullptr);
                if (ImGui::Button("Greet")) m_App.m_GameAPI->Emit("greet", m_App.m_GreetingEntity);
                ImGui::EndDisabled();
            }
            
            ImGui::End();
            return true;
        }
        
        RuntimeApplication& m_App;
    };

    void RegisterPanels()
    {
        auto& ui = m_VulkanRenderer->GetImGuiApplication();
        GramCheckVoid(ui.RegisterLayer<Gram::InspectorPanel>(m_Inspector, *m_World), "couldn't create the Inspector");
        GramCheckVoid(ui.RegisterLayer<Gram::SystemsPanel>(m_Systems, *m_GameAPI), "couldn't create the Systems panel");
        GramCheckVoid(ui.RegisterLayer<GameUI>(m_GameUI, *this), "couldn't create the game's UI");
        m_Inspector->Select(m_GreetingEntity);
    }
    
    std::shared_ptr<Gram::InspectorPanel> m_Inspector = nullptr;
    std::shared_ptr<Gram::SystemsPanel>   m_Systems   = nullptr;
    std::shared_ptr<GameUI>               m_GameUI    = nullptr;
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
