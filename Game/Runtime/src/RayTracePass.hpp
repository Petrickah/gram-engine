#pragma once

#include "BVHTree.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShaderPass.hpp"

#include "Passes.generated.hpp"

#ifdef GRAM_VULKAN_RENDERER

struct CameraControls
{
    glm::vec3 Position      = { 0.0f, 0.0f, 2.0f };
    float     Yaw           = 00.0f; // degrees, around Y axis; 0 = looks at -Z
    float     Pitch         = 00.0f; // degrees, up/down; limited at ±89
    float     FovY          = 45.0f; // degrees
    float     FStop         = 05.6f; // f/1.4 (wide open, most blur) ... f/22 (almost everything sharp)
    float     FocusDistance = 02.2f; // metres to the sharp plane (camera at z = 2, first sphere at z = -0.2)
    float     BokehScale    = 01.0f; // artistic: multiplies the physical lens radius (1 = a real camera)
    bool      PostProcessDoF = false; // false: the lens blurs in the ray tracer; true: a pinhole here, DepthOfField.hlsl blurs from the depth output
    uint32_t  MaxBounces    = 6;
};

// The GPU-side structs (RayTracerParams: the camera uniform; SphereData, MaterialData: the storage
// buffers' elements) and every binding number are generated from Content/shaders/RayTracer.pass.ron into
// Passes.generated.hpp, the same layout as RayTracer.generated.hlsli.

// What MaterialData::Type holds: the same numbers as RayTracer.hlsl's g_Material* constants.
enum class MaterialType : uint32_t { Lambertian, Metal, Dielectric };

// The game's scene: one complete scene raytraced from Content/shaders/RayTracer.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
// Its descriptors, pipeline, uniform and images are VulkanShaderPass's (from RayTracer.pass.ron); what's left
// here is the scene: packing the World, the BVH, the storage buffers and the dispatch.
class RayTracePass : public Gram::VulkanShaderPass<RayTracerParams>
{
public:
    RayTracePass(std::filesystem::path skybox, Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::World& world, const std::shared_ptr<Gram::VulkanRenderTarget>& target);
    
    void GenerateBookScene();

#ifdef GRAM_IMGUI_ENABLED
    void DrawRayTracerUI();
    void RegisterPanels(std::unique_ptr<Gram::Editor>& editor)
    {
        editor->AddPanel("Debug/Ray Tracer", [this] { DrawRayTracerUI(); });
    }
#endif // #ifdef GRAM_IMGUI_ENABLED

public:
    bool PackScene();

    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutput()      { return m_Output;      }
    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutputDepth() { return m_OutputDepth; }
    // The lens's settings, for the depth of field post-process (the same camera, simulated afterwards).
    const CameraControls& GetCameraControls() const { return m_CameraControls; }
    // The upscaling settings, for the Resample and Sharpen passes after it.
    bool  UsesFsr()       const { return m_Quality.Fsr && m_Quality.RenderScale < 1.0f; }
    float GetSharpening() const { return m_Quality.Sharpening; }

    bool ConsumeSceneChanged() { return std::exchange(m_SceneChanged, false); }
    
protected:
    bool OnPrepare(Gram::VulkanPass::Context ctx) override;
    void Fill(RayTracerParams& params, Gram::VulkanPass::Context ctx) override;
    bool OnRecord(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    Gram::World&                              m_World;
    std::shared_ptr<Gram::VulkanRenderTarget> m_Target;
    std::shared_ptr<Gram::VulkanRenderTarget> m_Output;
    std::shared_ptr<Gram::VulkanRenderTarget> m_OutputDepth;
    Gram::VulkanImage                         m_Skybox;

    bool           m_SceneChanged  = false;
    uint32_t       m_FrameCounter  = 0;
    uint32_t       m_SphereCount   = 0;
    uint32_t       m_MaterialCount = 0;
    CameraControls m_CameraControls;

    BVHTree           m_Tree;
    bool              m_UseBvh       = true;
    bool              m_RebuildTree  = false;                    // set by the Build combo: rebuild even if the scene didn't change
    BVHTree::Strategy m_TreeStrategy = BVHTree::Strategy::Sah;

    // The cost knobs of the Quality panel: the ray tracer renders at RenderScale times the screen's size on each
    // axis (under 1: magnified by the last pass; over 1: averaged, SSAA), and
    // Samples rays per pixel per frame (the accumulation averages them over frames anyway).
    struct QualityControls
    {
        float    RenderScale = 1.0f;   // 100%: the accumulation's jitter already supersamples a still image
        uint32_t Samples     = 1;
        bool     Fsr         = true;   // under 100%: FSR 1 (EASU + RCAS) rather than a bilinear magnification
        float    Sharpening  = 0.2f;   // RCAS, in stops below its strongest (0 = the most); AMD's default
    } m_Quality;

    bool           m_DebugWholePath = false;   // the heat map counts every bounce, not only the camera's ray
    uint32_t       m_DebugView = 0;      // the heat map: 0 off, 1 box tests, 2 sphere tests, 3 both
    uint32_t       m_BoxThreshold    = 64;    // the heat map: box tests for full red
    uint32_t       m_SphereThreshold = 16;    // ... sphere tests for full blue
    uint32_t       m_LargeCount = 0;   // Spheres[0, m_LargeCount): outside the tree (the ground)
    std::vector<SphereData> m_PackedSpheres;   // last frame's spheres in the World's order, before the tree reordered them
    BVHTree::Stats m_TreeStats;

    std::vector<MaterialData> m_Materials;
    std::vector<SphereData>   m_Spheres;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
