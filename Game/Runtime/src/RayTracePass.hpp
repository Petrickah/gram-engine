#pragma once

#include "Gram/Editor/Editor.hpp"
#include "Gram/Editor/EditorPanel.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#include <cstdint>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <memory>
#include <vector>

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
    uint32_t  MaxBounces    = 10;
};

struct CameraData
{
    glm::vec3 Position;      float    TanHalfFovY;
    glm::vec3 Right;         float    Aspect;
    glm::vec3 Up;            uint32_t SphereCount;
    glm::vec3 Forward;       uint32_t FrameIndex;
    uint32_t  Samples;       float    SampleScale;
    uint32_t  MaxBounces;    float    LensRadius;
    float     FocusDistance; float    Padding0[3];
};

struct MaterialData
{
    static constexpr std::array TypeNames { "Lambertian", "Metal", "Dielectric" };
    enum class TypeEnum : uint32_t
    {
        Lambertian,
        Metal,
        Dielectric,
        Count
    };

    glm::vec3 Albedo      ; float Fuzz              ;
    float     RefractIndex; float AbsorptionDistance; TypeEnum Type; float Padding0;
};

struct SphereData
{
    glm::vec3 Position   ; float    Radius       ;
    float     InnerRadius; uint32_t MaterialIndex; glm::vec2 Padding0;   // InnerRadius 0 = plină
};

static_assert(sizeof(CameraData)   == 96);
static_assert(sizeof(SphereData)   == 32);
static_assert(sizeof(MaterialData) == 32);
static_assert(MaterialData::TypeNames.size() == static_cast<size_t>(MaterialData::TypeEnum::Count));

// The game's scene: one complete scene raytraced from Content/shaders/RayTracer.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class RayTracePass : public Gram::VulkanPass
{
public:
    RayTracePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, const std::shared_ptr<Gram::VulkanRenderTarget>& target);

#ifdef GRAM_IMGUI_ENABLED
    // The game's own window: what only this game has.
    class RayTracerUI : public Gram::EditorPanel
    {
    public:
        RayTracerUI(RayTracePass& rayPass) : EditorPanel("RayTracer"), RayTracePass(rayPass) { }
        
        void DrawContents() override;
    
    private:
        RayTracePass& RayTracePass;
    };

    void RegisterPanels(std::unique_ptr<Gram::Editor>& editor)
    {
        // The scene target: the editor's Viewport shows what this RenderPass renders into it.
        editor->AddPanel<RayTracerUI>(*this);
    }
#endif // #ifdef GRAM_IMGUI_ENABLED

public:
    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutput() { return m_Output; }
    bool ConsumeSceneChanged() { return std::exchange(m_SceneChanged, false); }
    
private:
    std::shared_ptr<Gram::VulkanRenderTarget> m_Target;
    std::optional<uint64_t>                   m_TargetGeneration = std::nullopt;
    std::shared_ptr<Gram::VulkanRenderTarget> m_Output;
    Gram::VulkanDescriptors   m_Descriptors;
    Gram::VulkanPipeline      m_ComputePipeline;

    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_CameraUniforms;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_SphereStorages;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_MaterialStorages;

    bool           m_SceneChanged  = false;
    uint32_t       m_FrameCounter  = 0;
    uint32_t       m_SphereCount   = 0;
    uint32_t       m_MaterialCount = 0;
    CameraControls m_CameraControls;

    std::vector<MaterialData> m_Materials;
    std::vector<SphereData>   m_Spheres;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
