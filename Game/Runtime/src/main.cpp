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

// DepthOfField.hlsl's uniform. CoC = circle of confusion: the diameter, in input pixels, of the disc a point
// at depth d blurs into: CoCScale * |d - FocusDistance| / d, capped at MaxCoC.
struct DepthOfFieldParams
{
    glm::vec2 TexelSize; float FocusDistance; float    CoCScale;
    float     MaxCoC;    uint32_t Enabled;    glm::vec2 Padding0;
};

static_assert(sizeof(SSAAParams) == 16);
static_assert(sizeof(DepthOfFieldParams) == 32);
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

#ifdef GRAM_IMGUI_ENABLED
        rayTracePass.RegisterPanels(m_Editor);
#else
        rayTracePass.GenerateBookScene();
#endif // #ifdef GRAM_IMGUI_ENABLED

        const Gram::VulkanShader accumulate = Gram::VulkanShader::Graphics(spec.m_Workspace, "Accumulate", static_cast<vk::Device>(device.GetLogicalDevice()));
        auto& accumulatePass = m_VulkanRenderer->AddPass<PostProcessPass<AccumulationParams>>(device, accumulate, std::vector { rayTracePass.GetOutput() }, vk::Format::eR32G32B32A32Sfloat, [&state = m_Accumulation, &rayTracePass](const PostProcessPass<AccumulationParams>::Context& ctx)
        {
            if (state.Reset || ctx.OutputRecreated || rayTracePass.ConsumeSceneChanged()) { state.FrameCount = 0; state.Reset = false; }
            return AccumulationParams { .FrameCount = state.FrameCount++ };
        });

        // After the accumulation (it blurs the clean image), before SSAA (at the depth output's resolution).
        // Set 1: binding 0 = the accumulated color, binding 1 = the ray tracer's depth (metres along the camera's axis).
        const Gram::VulkanShader dof = Gram::VulkanShader::Graphics(spec.m_Workspace, "DepthOfField", static_cast<vk::Device>(device.GetLogicalDevice()));
        auto& dofPass = m_VulkanRenderer->AddPass<PostProcessPass<DepthOfFieldParams>>(device, dof, std::vector { accumulatePass.GetOutput(), rayTracePass.GetOutputDepth() }, vk::Format::eR32G32B32A32Sfloat, [&rayTracePass](const PostProcessPass<DepthOfFieldParams>::Context& ctx)
        {
            const CameraControls& c = rayTracePass.GetCameraControls();

            // The same lens as the ray tracer's (RayTracePass::Record): a full-frame sensor, the focal length
            // its FOV needs, the aperture's diameter A = f / N. A point at depth d is focused behind the sensor,
            // so it lands as a disc of diameter A * f * |d - s| / (d * (s - f)) on it (s = focus distance);
            // divided by the sensor's height, times the image's height in pixels, it's in pixels.
            constexpr float sensorHeight = 0.024f;
            const     float focalLength  = (sensorHeight * 0.5f) / std::tan(glm::radians(c.FovY) * 0.5f);
            const     float aperture     = focalLength / c.FStop;
            const     float focus        = std::max(c.FocusDistance, focalLength * 1.01f);   // s > f, or nothing focuses
            const     float cocScale     = aperture * focalLength / (focus - focalLength)
                                         / sensorHeight * static_cast<float>(ctx.InputExtent.height) * c.BokehScale;

            return DepthOfFieldParams {
                .TexelSize     = 1.0f / glm::vec2(ctx.InputExtent.width, ctx.InputExtent.height),
                .FocusDistance = focus,
                .CoCScale      = cocScale,
                .MaxCoC        = 64.0f,   // pixels of the 2x image: bounds the shader's loop
                .Enabled       = c.PostProcessDoF ? 1u : 0u,
            };
        });

        const Gram::VulkanShader ssaa = Gram::VulkanShader::Graphics(spec.m_Workspace, "SSAA", static_cast<vk::Device>(device.GetLogicalDevice()));
        m_VulkanRenderer->AddPass<PostProcessPass<SSAAParams>>(device, ssaa, std::vector { dofPass.GetOutput() }, m_VulkanRenderer->GetSceneTarget(), [](const PostProcessPass<SSAAParams>::Context& ctx)
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
        // The scene target: the editor's Viewport (Debug only) shows what the passes render into it.
        m_Editor = std::make_unique<Gram::Editor>(m_VulkanRenderer->GetImGuiApplication(), *m_World, *m_GameAPI, m_VulkanRenderer->GetSceneTarget());
        // The game's own panels: in Debug next to the engine's, in Release the only ones, to test the game with.
        m_Editor->AddPanel<GameUI>(*this);
        GramEditorOnly(m_Editor->GetInspector().Select(m_GreetingEntity));
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
