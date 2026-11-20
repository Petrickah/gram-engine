#include "Gram/EntryPoint.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Scripting/ECS/World.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Editor/InspectorPanel.hpp"

#include "Components.generated.hpp"
#include "Passes.generated.hpp"
#include "PostProcessPass.hpp"
#include "RayTracePass.hpp"

class RuntimeApplication : public Gram::GLFWApplication
{
public:
    RuntimeApplication(const Gram::GLFWApplicationSpecification& spec) : GLFWApplication(spec), m_Workspace(spec.m_Workspace)
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

        const Gram::VulkanShader shader = Gram::VulkanShader::Compute(spec.m_Workspace, RayTracerParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
        RayTracePass& rayTracePass = m_VulkanRenderer->AddPass<RayTracePass>(spec.m_Workspace / "textures" / "autumn_field_puresky_4k.hdr", device, shader, *m_World, m_VulkanRenderer->GetSceneTarget());

#ifdef GRAM_IMGUI_ENABLED
        rayTracePass.RegisterPanels(m_Editor);
#else
        rayTracePass.GenerateBookScene();
#endif // #ifdef GRAM_IMGUI_ENABLED

        const Gram::VulkanShader accumulate = Gram::VulkanShader::Graphics(spec.m_Workspace, AccumulateParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
        auto& accumulatePass = m_VulkanRenderer->AddPass<PostProcessPass<AccumulateParams>>(device, accumulate, std::vector { rayTracePass.GetOutput() }, vk::Format::eR32G32B32A32Sfloat, [&state = m_Accumulation, &rayTracePass](const PostProcessPass<AccumulateParams>::Context& ctx)
        {
            if (state.Reset || ctx.OutputRecreated || rayTracePass.ConsumeSceneChanged()) { state.FrameCount = 0; state.Reset = false; }
            return AccumulateParams { .FrameCount = state.FrameCount++ };
        });

        // After the accumulation (it blurs the clean image), before Resample (at the depth output's resolution).
        // Set 1: binding 0 = the accumulated color, binding 1 = the ray tracer's depth (metres along the camera's axis).
        const Gram::VulkanShader dof = Gram::VulkanShader::Graphics(spec.m_Workspace, DepthOfFieldParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
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
                .CocScale      = cocScale,
                .MaxCoc        = 64.0f,   // pixels of the 2x image: bounds the shader's loop
                .Enabled       = c.PostProcessDoF ? 1u : 0u,
            };
        });

        // Resample: from the ray tracer's resolution to the screen's, into an image of the scene's size. Over
        // 100% it averages (SSAA); under, it magnifies, bilinearly or with FSR 1's EASU.
        const Gram::VulkanShader resample = Gram::VulkanShader::Graphics(spec.m_Workspace, ResampleParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
        auto& resamplePass = m_VulkanRenderer->AddPass<PostProcessPass<ResampleParams>>(device, resample, std::vector { dofPass.GetOutput() }, vk::Format::eR32G32B32A32Sfloat, m_VulkanRenderer->GetSceneTarget(), [&rayTracePass](const PostProcessPass<ResampleParams>::Context& ctx)
        {
            const glm::vec2 in  = glm::vec2(ctx.InputExtent.width,  ctx.InputExtent.height);
            const glm::vec2 out = glm::vec2(ctx.OutputExtent.width, ctx.OutputExtent.height);

            // FsrEasuCon (ffx_fsr1.h), the input being the whole image: con0 maps a screen pixel to the input's
            // pixel grid; con1..con3 are the offsets, in UV, of the 4 gathers around it (its 12-texel kernel).
            return ResampleParams {
                .TexelSize   = 1.0f / in,
                .ScaleFactor = in.x / out.x,
                .Mode        = rayTracePass.UsesFsr() ? 1u : 0u,
                .EasuCon0    = glm::vec4(in.x / out.x, in.y / out.y, 0.5f * in.x / out.x - 0.5f, 0.5f * in.y / out.y - 0.5f),
                .EasuCon1    = glm::vec4( 1.0f / in.x,  1.0f / in.y, 1.0f / in.x, -1.0f / in.y),
                .EasuCon2    = glm::vec4(-1.0f / in.x,  2.0f / in.y, 1.0f / in.x,  2.0f / in.y),
                .EasuCon3    = glm::vec4( 0.0f,         4.0f / in.y, 0.0f,         0.0f),
            };
        });

        // Sharpen: FSR 1's RCAS on the magnified image, into the scene; a plain copy without FSR.
        const Gram::VulkanShader sharpen = Gram::VulkanShader::Graphics(spec.m_Workspace, SharpenParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
        m_VulkanRenderer->AddPass<PostProcessPass<SharpenParams>>(device, sharpen, std::vector { resamplePass.GetOutput() }, m_VulkanRenderer->GetSceneTarget(), [&rayTracePass](const PostProcessPass<SharpenParams>::Context&)
        {
            return SharpenParams {
                .Sharpness = std::exp2(-rayTracePass.GetSharpening()),   // stops below the strongest → RCAS's linear strength
                .Enabled   = rayTracePass.UsesFsr() ? 1u : 0u,
            };
        });

        // Kept for backwards compatibility: used to test graphics shaders / pipeline and upload data through buffers.
        // const Gram::VulkanShader triangle = Gram::VulkanShader::Graphics(spec.m_Workspace, DefaultParams::Shader, static_cast<vk::Device>(device.GetLogicalDevice()));
        // m_VulkanRenderer->AddPass<TrianglePass>(spec.m_Workspace / "textures" / "texture.jpg", device, triangle, *m_VulkanRenderer->GetSceneTarget());
    }
#endif // #ifdef GRAM_VULKAN_RENDERER

    std::filesystem::path m_Workspace;   // the executable's folder: screenshots go in screenshots/ there
    Gram::ObjectID m_GreetingEntity = {};
    AccumulateParams m_Accumulation = {};
    std::shared_ptr<Gram::GameAPILoader> m_GameAPI;
    std::shared_ptr<Gram::World> m_World;

#ifdef GRAM_IMGUI_ENABLED
    // The game's own window: what only this game has (the Greet button).
    void DrawGameUI()
    {
        ImGui::BeginDisabled(m_GameAPI->GetGameAPI() == nullptr);
        if (ImGui::Button("Greet")) m_GameAPI->Emit("greet", m_GreetingEntity);
        ImGui::EndDisabled();
    }

    // The scene's resolution (the viewport's own, or a fixed one shown letterboxed) and V-Sync.
    void DrawDisplayUI()
    {
        const std::shared_ptr<Gram::VulkanRenderTarget>& scene = m_VulkanRenderer->GetSceneTarget();
        const vk::Extent2D extent = scene->GetImage().GetExtent();
        ImGui::Text("Scene: %u x %u", extent.width, extent.height);

        static constexpr std::array<vk::Extent2D, 4> fixed { vk::Extent2D { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
        static constexpr std::array names { "Fit the viewport", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160" };
        int current = 0;   // index into names: 0 = no fixed size
        if (const std::optional<vk::Extent2D> pinned = scene->GetFixedExtent())
        {
            for (size_t i = 0; i < fixed.size(); i++) if (fixed[i] == *pinned) current = static_cast<int>(i) + 1;
        }
        if (ImGui::Combo("Resolution", &current, names.data(), static_cast<int>(names.size())))
        {
            scene->SetFixedExtent(current == 0 ? std::nullopt : std::optional(fixed[current - 1]));   // applied next frame
        }

        bool vsync = m_VulkanRenderer->GetVSync();
        if (ImGui::Checkbox("V-Sync", &vsync)) m_VulkanRenderer->SetVSync(vsync);   // the swapchain is recreated next frame

        // The scene as it is, without the editor around it, at its own resolution: a PNG named after the time,
        // in screenshots/ next to the executable.
        if (ImGui::Button("Take Picture"))
        {
            const std::string name = std::format("{:%Y-%m-%d_%H-%M-%S}.png", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            m_VulkanRenderer->RequestScreenshot(m_Workspace / "screenshots" / name);
        }
    }

    void RegisterPanels()
    {
        // The scene target: the editor's Viewport (Debug only) shows what the passes render into it.
        m_Editor = std::make_unique<Gram::Editor>(m_VulkanRenderer->GetImGuiApplication(), *m_World, *m_GameAPI, m_VulkanRenderer->GetSceneTarget(), &m_VulkanRenderer->GetProfiler());
        // The game's own panels: in Debug next to the engine's, in Release the only ones, to test the game with.
        m_Editor->AddPanel("Debug/Greeting", [this] { DrawGameUI(); });
        m_Editor->AddPanel("Debug/Display",  [this] { DrawDisplayUI(); });
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
