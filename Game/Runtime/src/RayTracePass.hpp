#pragma once

#include "BVHTree.hpp"
#include "Gram/Editor/Editor.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

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
    uint32_t  MaxBounces    = 10;
};

// The GPU-side structs (RayTracerParams: the camera uniform; SphereData, MaterialData: the storage
// buffers' elements) and every binding number are generated from Content/shaders/RayTracer.pass.ron into
// Passes.generated.hpp, the same layout as RayTracer.generated.hlsli.

// What MaterialData::Type holds: the same numbers as RayTracer.hlsl's g_Material* constants.
enum class MaterialType : uint32_t { Lambertian, Metal, Dielectric };

// The game's scene: one complete scene raytraced from Content/shaders/RayTracer.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class RayTracePass : public Gram::VulkanPass
{
public:
    RayTracePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::World& world, const std::shared_ptr<Gram::VulkanRenderTarget>& target);
    
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
    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutput()      { return m_Output;      }
    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutputDepth() { return m_OutputDepth; }
    // The lens's settings, for the depth of field post-process (the same camera, simulated afterwards).
    const CameraControls& GetCameraControls() const { return m_CameraControls; }

    bool ConsumeSceneChanged() { return std::exchange(m_SceneChanged, false); }
    
private:
    Gram::World&                              m_World;
    std::shared_ptr<Gram::VulkanRenderTarget> m_Target;
    std::optional<uint64_t>                   m_TargetGeneration = std::nullopt;
    std::optional<uint64_t>                   m_DepthGeneration  = std::nullopt;
    std::shared_ptr<Gram::VulkanRenderTarget> m_Output;
    std::shared_ptr<Gram::VulkanRenderTarget> m_OutputDepth;
    Gram::VulkanDescriptors                   m_Descriptors;
    Gram::VulkanDescriptors                   m_ImageDescriptors;
    Gram::VulkanPipeline                      m_ComputePipeline;

    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_CameraUniforms;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_SphereStorages;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_MaterialStorages;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_NodeStorages;

    bool           m_SceneChanged  = false;
    uint32_t       m_FrameCounter  = 0;
    uint32_t       m_SphereCount   = 0;
    uint32_t       m_MaterialCount = 0;
    CameraControls m_CameraControls;

    BVHTree        m_Tree;
    bool           m_UseBvh = false;
    bool           m_DebugView = false;
    uint32_t       m_LargeCount = 0;   // Spheres[0, m_LargeCount): outside the tree (the ground)
    std::vector<SphereData> m_PackedSpheres;   // last frame's spheres in the World's order, before the tree reordered them
    BVHTree::Stats m_TreeStats;

    std::vector<MaterialData> m_Materials;
    std::vector<SphereData>   m_Spheres;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
