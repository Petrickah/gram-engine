#include "Gram/EntryPoint.hpp"
#include "Gram/Application/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Editor/InspectorPanel.hpp"

#include "Components.generated.hpp"
#include "TrianglePass.hpp"

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

        GramVulkanOnly(RegisterPasses(spec));

        // After the World and the loader: the panels keep references to them.
        GramImGuiOnly(RegisterPanels());
    }

private:
#ifdef GRAM_VULKAN_RENDERER
    // The game's render passes, drawn in the order they're added, before the editor's UI.
    void RegisterPasses(const Gram::GLFWApplicationSpecification& spec)
    {
        GramCheckVoid(m_VulkanRenderer, "couldn't register the render passes: no Vulkan renderer");

        Gram::VulkanDevice& device = m_VulkanRenderer->GetDevice();
        const Gram::VulkanShader shader = Gram::VulkanShader::Graphics(spec.m_Workspace, "Default", static_cast<vk::Device>(device.GetLogicalDevice()));
        m_VulkanRenderer->AddPass<TrianglePass>(device, m_VulkanRenderer->GetSwapchain(), shader);
    }
#endif // #ifdef GRAM_VULKAN_RENDERER

    Gram::ObjectID m_GreetingEntity = {};
    std::shared_ptr<Gram::GameAPILoader> m_GameAPI;
    std::shared_ptr<Gram::World> m_World;

#ifdef GRAM_IMGUI_ENABLED
    // The game's own window: what only this game has (the Greet button).
    struct GameUI : Gram::EditorPanel
    {
        GameUI(RuntimeApplication& app) : EditorPanel("Game"), m_App(app) { }
        
        void DrawContents() override
        {
            ImGui::BeginDisabled(m_App.m_GameAPI->GetGameAPI() == nullptr);
            if (ImGui::Button("Greet")) m_App.m_GameAPI->Emit("greet", m_App.m_GreetingEntity);
            ImGui::EndDisabled();
        }
        
        RuntimeApplication& m_App;
    };

    void RegisterPanels()
    {
        m_Editor = std::make_unique<Gram::Editor>(m_VulkanRenderer->GetImGuiApplication(), *m_World, *m_GameAPI);
        m_Editor->AddPanel<GameUI>(*this);
        m_Editor->GetInspector().Select(m_GreetingEntity);
    }
    
    std::unique_ptr<Gram::Editor> m_Editor = nullptr;
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
            .m_WindowSize    = glm::vec2(1920, 1080),
            .m_WindowTitle   = "Gram Application with Vulkan",
            .m_Workspace     = Gram::GetExecutableDirectory(executableFile)
        }
    );
}
