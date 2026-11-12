#include "Gram/EntryPoint.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Editor/InspectorPanel.hpp"

#include "Components.generated.hpp"
#include "PostProcessPass.hpp"
#include "RayTracePass.hpp"

struct SSAAParams
{
    glm::vec2 TexelSize; float ScaleFactor; float Padding0;
};

struct AccumulationParams
{
    uint32_t FrameCount = 0; bool Reset = true;
};

static_assert(sizeof(SSAAParams) == 16);
static_assert(sizeof(AccumulationParams) == 8);

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

        GramVulkanOnly(RegisterPasses(spec));
    }

private:
#ifdef GRAM_VULKAN_RENDERER
    // The game's render passes, drawn in the order they're added, before the editor's UI.
    void RegisterPasses(const Gram::GLFWApplicationSpecification& spec)
    {
        GramCheckVoid(m_VulkanRenderer, "couldn't register the render passes: no Vulkan renderer");

        Gram::VulkanDevice& device = m_VulkanRenderer->GetDevice();

        const Gram::VulkanShader shader = Gram::VulkanShader::Compute(spec.m_Workspace, "RayTracer", static_cast<vk::Device>(device.GetLogicalDevice()));
        RayTracePass& rayTracePass = m_VulkanRenderer->AddPass<RayTracePass>(device, shader, m_VulkanRenderer->GetSceneTarget());
        GramImGuiOnly(rayTracePass.RegisterPanels(m_Editor));

        const Gram::VulkanShader accumulate = Gram::VulkanShader::Graphics(spec.m_Workspace, "Accumulate", static_cast<vk::Device>(device.GetLogicalDevice()));
        auto& accumulatePass = m_VulkanRenderer->AddPass<PostProcessPass<AccumulationParams>>(device, accumulate, rayTracePass.GetOutput(), vk::Format::eR32G32B32A32Sfloat, [&state = m_Accumulation, &rayTracePass](const PostProcessPass<AccumulationParams>::Context& ctx)
        {
            if (state.Reset || ctx.OutputRecreated || rayTracePass.ConsumeSceneChanged()) { state.FrameCount = 0; state.Reset = false; }
            return AccumulationParams { .FrameCount = state.FrameCount++ };
        });

        const Gram::VulkanShader ssaa = Gram::VulkanShader::Graphics(spec.m_Workspace, "SSAA", static_cast<vk::Device>(device.GetLogicalDevice()));
        m_VulkanRenderer->AddPass<PostProcessPass<SSAAParams>>(device, ssaa, accumulatePass.GetOutput(), m_VulkanRenderer->GetSceneTarget(), [](const PostProcessPass<SSAAParams>::Context& ctx)
        {
            return SSAAParams {
                .TexelSize   = 1.0f / glm::vec2(ctx.InputExtent.width, ctx.InputExtent.height),
                .ScaleFactor = static_cast<float>(ctx.InputExtent.width) / static_cast<float>(ctx.OutputExtent.width),
            };
        });

        // Kept for backwards compatibility: used to test graphics shaders / pipeline and upload data through buffers.
        // const Gram::VulkanShader shader = Gram::VulkanShader::Graphics(spec.m_Workspace, "Default", static_cast<vk::Device>(device.GetLogicalDevice()));
        // m_VulkanRenderer->AddPass<TrianglePass>(spec.m_Workspace / "textures" / "texture.jpg", device, shader, m_VulkanRenderer->GetSceneTarget());
    }
#endif // #ifdef GRAM_VULKAN_RENDERER

    Gram::ObjectID m_GreetingEntity = {};
    AccumulationParams m_Accumulation = {};
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
        // The scene target: the editor's Viewport shows what TrianglePass renders into it.
        m_Editor = std::make_unique<Gram::Editor>(m_VulkanRenderer->GetImGuiApplication(), *m_World, *m_GameAPI, m_VulkanRenderer->GetSceneTarget());
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
