#include "Components.generated.hpp"
#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Scripting/ECS/World.hpp"

#include "RayTracePass.hpp"
#include "BVHTree.hpp"
#include <chrono>
#include <random>

#ifdef GRAM_VULKAN_RENDERER

RayTracePass::RayTracePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::World& world, const std::shared_ptr<Gram::VulkanRenderTarget>& target)
    : m_World(world)
    , m_Target(target)
    , m_Descriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, std::vector<vk::DescriptorSetLayoutBinding> {
        { .binding = RayTracerParams::UniformBinding,   .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
        { .binding = RayTracerParams::SpheresBinding,   .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
        { .binding = RayTracerParams::MaterialsBinding, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
        { .binding = RayTracerParams::NodesBinding,     .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }
    }))
    , m_ImageDescriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, std::vector<vk::DescriptorSetLayoutBinding> {
        { .binding = RayTracerParams::ResultBinding,    .descriptorType = vk::DescriptorType::eStorageImage , .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
        { .binding = RayTracerParams::DepthBinding,     .descriptorType = vk::DescriptorType::eStorageImage , .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
    }))
    , m_ComputePipeline(Gram::VulkanPipeline::Compute(device, shader, std::array { m_Descriptors.GetLayout(), m_ImageDescriptors.GetLayout() }))
    , m_CameraUniforms(Gram::VulkanBuffer::Create<Gram::UniformBuffer<RayTracerParams>>(device))
    , m_SphereCount(512), m_MaterialCount(512)
{
    m_Output      = target ? std::make_shared<Gram::VulkanRenderTarget>(device, target->GetImage().GetExtent(), vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled) : nullptr;
    m_OutputDepth = target ? std::make_shared<Gram::VulkanRenderTarget>(device, target->GetImage().GetExtent(), vk::Format::eR32Sfloat,          vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled) : nullptr;
    GramCheckVoid(m_Output && m_OutputDepth, "the output image was not created; no render target recieved");

    Gram::VulkanImage& scene      = m_Output->GetImage();
    Gram::VulkanImage& sceneDepth = m_OutputDepth->GetImage();
    if (scene.IsValid())      m_ImageDescriptors.Write(device, RayTracerParams::ResultBinding, scene, vk::ImageLayout::eGeneral);
    if (sceneDepth.IsValid()) m_ImageDescriptors.Write(device, RayTracerParams::DepthBinding, sceneDepth, vk::ImageLayout::eGeneral);

    m_SphereStorages   = Gram::VulkanBuffer::Create<Gram::StorageBuffer<SphereData>>(device, m_SphereCount);
    m_MaterialStorages = Gram::VulkanBuffer::Create<Gram::StorageBuffer<MaterialData>>(device, m_MaterialCount);
    m_Descriptors.Write(device, RayTracerParams::SpheresBinding, m_SphereStorages  , m_SphereCount   * sizeof(SphereData));
    m_Descriptors.Write(device, RayTracerParams::MaterialsBinding, m_MaterialStorages, m_MaterialCount * sizeof(MaterialData));

    // A binary tree over N spheres has at most 2N - 1 nodes.
    m_NodeStorages = Gram::VulkanBuffer::Create<Gram::StorageBuffer<BVHNode>>(device, 2 * m_SphereCount);
    m_Descriptors.Write(device, RayTracerParams::NodesBinding, m_NodeStorages, 2 * m_SphereCount * sizeof(BVHNode));
    m_Descriptors.Write(device, RayTracerParams::UniformBinding, m_CameraUniforms  , sizeof(RayTracerParams));
}

// The book's final scene (ch. 14): a ground, a 22×22 grid of small random spheres, three big ones.
void RayTracePass::GenerateBookScene()
{
    // Two passes: destroying inside Each would move the storage being walked.
    std::vector<Gram::ObjectID> old;
    m_World.Each<Gram::Sphere>([&](Gram::ObjectID entity, Gram::Sphere&) { old.push_back(entity); });
    for (const Gram::ObjectID& entity : old) m_World.DestroyEntity(entity);

    std::mt19937 rng(1337);   // fixed seed: the same scene every time
    auto random = [&](float lo = 0.0f, float hi = 1.0f) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    auto randomColor = [&](float lo = 0.0f, float hi = 1.0f) { return glm::vec3(random(lo, hi), random(lo, hi), random(lo, hi)); };

    auto add = [&](glm::vec3 position, float radius, Gram::Material material)
    {
        Gram::ObjectID entity = m_World.CreateEntity();
        m_World.Add<Gram::Material>(entity, material);
        m_World.Add<Gram::Sphere>(entity, {
            .Center      = position, 
            .Radius      = radius, 
            .InnerRadius = 0.0f
        });
    };

    using Type = MaterialType;
    add({ 0.0f, -1000.0f, 0.0f }, 1000.0f, Gram::Material { .Albedo = glm::vec3(0.5f), .Kind = static_cast<uint32_t>(Type::Lambertian) });

    for (int a = -11; a < 11; a++)
    {
        for (int b = -11; b < 11; b++)
        {
            const float     choose = random();
            const glm::vec3 center(a + 0.9f * random(), 0.2f, b + 0.9f * random());
            if (glm::length(center - glm::vec3(4.0f, 0.2f, 0.0f)) <= 0.9f) continue;   // room for the big metal one

            if      (choose < 0.80f) add(center, 0.2f, Gram::Material { .Albedo = randomColor() * randomColor(), .Kind = static_cast<uint32_t>(Type::Lambertian) });
            else if (choose < 0.95f) add(center, 0.2f, Gram::Material { .Albedo = randomColor(0.5f, 1.0f), .Fuzz = random(0.0f, 0.5f), .Kind = static_cast<uint32_t>(Type::Metal) });
            else                     add(center, 0.2f, Gram::Material { .Albedo = glm::vec3(1.0f), .RefractIndex = 1.5f, .AbsorptionDistance = 1.0f, .Kind = static_cast<uint32_t>(Type::Dielectric) });
        }
    }

    add({  0.0f, 1.0f, 0.0f }, 1.0f, Gram::Material { .Albedo = glm::vec3(1.0f), .RefractIndex = 1.5f, .AbsorptionDistance = 1.0f, .Kind = static_cast<uint32_t>(Type::Dielectric) });
    add({ -4.0f, 1.0f, 0.0f }, 1.0f, Gram::Material { .Albedo = { 0.4f, 0.2f, 0.1f }, .Kind = static_cast<uint32_t>(Type::Lambertian) });
    add({  4.0f, 1.0f, 0.0f }, 1.0f, Gram::Material { .Albedo = { 0.7f, 0.6f, 0.5f }, .Fuzz = 0.0f, .Kind = static_cast<uint32_t>(Type::Metal) });

    // The book's camera: from (13, 2, 3) looking at the origin, vfov 20°, focused at 10.
    const glm::vec3 forward = glm::normalize(glm::vec3(0.0f) - glm::vec3(13.0f, 2.0f, 3.0f));
    m_CameraControls.Position      = { 13.0f, 2.0f, 3.0f };
    m_CameraControls.Pitch         = glm::degrees(std::asin(forward.y));              // inversul formulei din Record
    m_CameraControls.Yaw           = glm::degrees(std::atan2(forward.x, -forward.z));
    m_CameraControls.FovY          = 20.0f;
    m_CameraControls.FocusDistance = 10.0f;
}

// The GPU's copy of the scene, rebuilt from the World every frame: one material per sphere, in the
// same order, so sphere i's material is element i. Returns whether it differs from the last frame's.
bool RayTracePass::PackScene()
{
    std::vector<SphereData>   spheres;
    std::vector<MaterialData> materials;

    m_World.Each<Gram::Sphere>([&](Gram::ObjectID entity, Gram::Sphere& sphere)
    {
        const Gram::Material* material = m_World.Get<Gram::Material>(entity);
        if (!material || spheres.size() >= m_SphereCount) return;   // fără material nu are ce randa; bufferul e plin

        materials.push_back({
            .Albedo             = material->Albedo,
            .Fuzz               = material->Fuzz,
            .RefractIndex       = material->RefractIndex,
            .AbsorptionDistance = material->AbsorptionDistance,
            .Type               = material->Kind,   // MaterialType's numbers
        });
        spheres.push_back({
            .Position      = sphere.Center,
            .Radius        = sphere.Radius,
            .InnerRadius   = sphere.InnerRadius,
            .MaterialIndex = static_cast<uint32_t>(materials.size() - 1),
        });
    });

    auto differs = [](const auto& a, const auto& b)
    {
        return a.size() != b.size() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) != 0;
    };
    // Compared with the last frame's array as it came from the World, not with m_Spheres: building the tree
    // reorders the spheres, so a scene that didn't move would look changed every frame.
    const bool changed = differs(spheres, m_PackedSpheres) || differs(materials, m_Materials);
    m_PackedSpheres = spheres;

    if (changed || std::exchange(m_RebuildTree, false))
    {
        // Huge spheres (the ground) go first and stay out of the tree: one of them would make every box on its
        // way down as big as itself, and nearly every ray would enter them all. They're tested one by one.
        constexpr float largeRadius = 10.0f;
        auto large   = std::partition(spheres.begin(), spheres.end(), [&](const SphereData& s) { return s.Radius > largeRadius; });
        m_LargeCount = static_cast<uint32_t>(large - spheres.begin());
        const std::span<SphereData> treeSpheres = std::span(spheres).subspan(m_LargeCount);   // the tree's indices start after them

        const auto start = std::chrono::steady_clock::now();
        m_Tree = BVHTree::Create(treeSpheres, m_TreeStrategy);
        const auto end   = std::chrono::steady_clock::now();

        m_TreeStats         = m_Tree.Validate(treeSpheres);
        m_TreeStats.BuildMs = std::chrono::duration<float, std::milli>(end - start).count();

        // Only together with the tree: the nodes point at the spheres in the order Create left them in, and
        // this frame's `spheres` is in that order only when the tree was just built from it.
        m_Spheres   = std::move(spheres);
        m_Materials = std::move(materials);
    }

    return changed;
}

bool RayTracePass::Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    GramCheck(m_ComputePipeline.IsValid(), "RayTracePass: the compute pipeline wasn't created");
    GramCheck(m_Output && m_OutputDepth && m_Target, "RayTracePass: at least one of the images were nullptr");
    
    if (PackScene()) m_SceneChanged = true;

    m_Output     ->RequestExtent(m_Target->GetImage().GetExtent(), static_cast<float>(m_Quality.Ssaa));
    m_Output     ->ApplyRequestedExtent();
    m_OutputDepth->RequestExtent(m_Target->GetImage().GetExtent(), static_cast<float>(m_Quality.Ssaa));
    m_OutputDepth->ApplyRequestedExtent();

    // The image's own size, not the swapchain's: the ray tracer fills the target, whatever the window.
    Gram::VulkanImage& scene       = m_Output     ->GetImage();
    Gram::VulkanImage& sceneDepth  = m_OutputDepth->GetImage();
    const vk::Extent2D extent      = scene         .GetExtent();
    const vk::Extent2D extentDepth = sceneDepth    .GetExtent();

    scene     .Transition(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);
    sceneDepth.Transition(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);

    // The target was recreated (a resize): the sets still point at the old image's view.
    const bool outputRecreated = !m_TargetGeneration || *m_TargetGeneration != m_Output->GetGeneration(); m_TargetGeneration = m_Output->GetGeneration();
    const bool outputDepthRecreated = !m_DepthGeneration || *m_DepthGeneration != m_OutputDepth->GetGeneration(); m_DepthGeneration = m_OutputDepth->GetGeneration();

    // Safe here: ApplyRequestedExtent waited for the device before recreating the image,
    // so no command buffer uses these sets anymore, not even the other frame in flight's.
    if (outputRecreated)      m_ImageDescriptors.Write(ctx.Device, RayTracerParams::ResultBinding, m_Output     ->GetImage(), vk::ImageLayout::eGeneral);
    if (outputDepthRecreated) m_ImageDescriptors.Write(ctx.Device, RayTracerParams::DepthBinding, m_OutputDepth->GetImage(), vk::ImageLayout::eGeneral);

    m_ComputePipeline.Bind(cmd);
    m_ComputePipeline.BindDescriptorSets(cmd, 0, std::array { m_Descriptors.Get(ctx.ActiveFrame.FrameIndex), m_ImageDescriptors.Get(ctx.ActiveFrame.FrameIndex) });

    m_CameraUniforms[ctx.ActiveFrame.FrameIndex].Send(cmd, [&](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        const float yaw   = glm::radians(m_CameraControls.Yaw);
        const float pitch = glm::radians(m_CameraControls.Pitch);

        const glm::vec3 forward = glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)));
        const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));   // lumea are Y în sus
        const glm::vec3 up      = glm::cross(right, forward);

        // One unit = 1 m. The focal length a full-frame camera (a 24 mm tall sensor) needs for this FOV, and
        // the lens's radius at this f-stop: N = focal length / aperture diameter, so radius = f / (2N).
        constexpr float sensorHeight = 0.024f;
        const     float tanHalfFovY  = std::tan(glm::radians(m_CameraControls.FovY) * 0.5f);
        const     float focalLength  = (sensorHeight * 0.5f) / tanHalfFovY;
        // With the post-process depth of field, the ray tracer stays a pinhole: everything sharp, blurred afterwards.
        const     float lensRadius   = m_CameraControls.PostProcessDoF ? 0.0f : focalLength / (2.0f * m_CameraControls.FStop) * m_CameraControls.BokehScale;

        RayTracerParams camera {
            .Position      = m_CameraControls.Position,      .TanHalfFovY  = tanHalfFovY,
            .Right         = right,                          .Aspect       = static_cast<float>(extent.width) / static_cast<float>(extent.height),
            .Up            = up,                             .SphereCount  = static_cast<uint32_t>(m_Spheres.size()),
            .Forward       = forward,                        .FrameIndex   = m_FrameCounter++,
            .Samples       = m_Quality.Samples,              .SampleScale  = 1.0f / static_cast<float>(m_Quality.Samples),
            .MaxBounces    = m_CameraControls.MaxBounces,    .LensRadius   = lensRadius,
            .FocusDistance = m_CameraControls.FocusDistance,
            .NodeCount     = static_cast<uint32_t>(m_Tree.GetNodes().size()),
            .UseBvh        = m_UseBvh ? 1u : 0u,
            .LargeCount    = m_LargeCount,
            .DebugView     = m_DebugView ? 1u : 0u,
        };

        buffer.Map<RayTracerParams, Gram::VulkanBuffer::Access::Write>().Write(&camera, 0, 1);
    });

    m_SphereStorages[ctx.ActiveFrame.FrameIndex].Send(cmd, [this](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        buffer.Map<SphereData, Gram::VulkanBuffer::Access::Write>().Write(m_Spheres.data(), 0, m_Spheres.size());
    });

    m_NodeStorages[ctx.ActiveFrame.FrameIndex].Send(cmd, [this](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        const std::vector<BVHNode>& nodes = m_Tree.GetNodes();
        if (!nodes.empty()) buffer.Map<BVHNode, Gram::VulkanBuffer::Access::Write>().Write(nodes.data(), 0, nodes.size());
    });

    m_MaterialStorages[ctx.ActiveFrame.FrameIndex].Send(cmd, [this](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        buffer.Map<MaterialData, Gram::VulkanBuffer::Access::Write>().Write(m_Materials.data(), 0, m_Materials.size());
    });

    cmd.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);

    // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering,
    // where no barrier can go.
    scene     .Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    sceneDepth.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    return true;
}

#ifdef GRAM_IMGUI_ENABLED
void RayTracePass::DrawRayTracerUI()
{
    if (ImGui::CollapsingHeader("BVH", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const BVHTree::Stats& s = m_TreeStats;
        ImGui::Text("%u nodes, %u leaves, depth %u, fullest leaf %u", s.Nodes, s.Leaves, s.MaxDepth, s.MaxLeafSize);
        ImGui::Text("built in %.3f ms", s.BuildMs);
        if (s.Error.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "valid");
        else                 ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "invalid: %s", s.Error.c_str());

        // Off: every ray tests every sphere (the old loop); on: through the tree. The same image either way.
        int strategy = static_cast<int>(m_TreeStrategy);
        if (ImGui::Combo("Build", &strategy, "Midpoint\0SAH\0"))   // rebuilt next frame; the image stays the same
        {
            m_TreeStrategy = static_cast<BVHTree::Strategy>(strategy);
            m_RebuildTree  = true;
        }

        if (ImGui::Checkbox("Use BVH", &m_UseBvh)) m_SceneChanged = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Show cost", &m_DebugView)) m_SceneChanged = true;   // the heat map: tests per primary ray
        ImGui::Text("%u large sphere(s) outside the tree", m_LargeCount);
    }


    bool changed = false;
    ImGui::BeginDisabled(m_Spheres.size() >= m_SphereCount);
    if (ImGui::Button("Add Sphere"))
    {
        const Gram::ObjectID entity = m_World.CreateEntity();
        m_World.Add(entity, Gram::Sphere {
            .Center = { 0.0f, 0.0f, -0.2f },
            .Radius = 0.45f,
            .InnerRadius = 0.0f
        });
        m_World.Add(entity, Gram::Material {
            .Albedo = { 0.8f, 0.3f, 0.3f },
            .Fuzz = 0.5f,
            .RefractIndex = 1.5f,
            .AbsorptionDistance = 1.0f,
            .Kind = static_cast<uint32_t>(MaterialType::Lambertian)
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Generate Scene"))
    {
        GenerateBookScene(); changed = true;
    }

    // What makes a frame expensive: pixels (SSAA renders Ssaa x Ssaa rays per screen pixel), rays per pixel
    // per frame (samples), and the length of each ray's path (bounces).
    if (ImGui::CollapsingHeader("Quality", ImGuiTreeNodeFlags_DefaultOpen))
    {
        constexpr std::array ssaaFactors { 1u, 2u, 4u };
        constexpr std::array ssaaNames   { "1x (off)", "2x", "4x" };
        int ssaa = static_cast<int>(std::ranges::find(ssaaFactors, m_Quality.Ssaa) - ssaaFactors.begin());
        if (ImGui::Combo("SSAA", &ssaa, ssaaNames.data(), static_cast<int>(ssaaNames.size())))
        {
            m_Quality.Ssaa = ssaaFactors[ssaa];   // the images are recreated next frame, which resets the accumulation
        }

        const uint32_t minSamples = 1, maxSamples = 16, minBounces = 1, maxBounces = 100;
        changed |= ImGui::SliderScalar("Samples per frame", ImGuiDataType_U32, &m_Quality.Samples, &minSamples, &maxSamples);
        changed |= ImGui::SliderScalar("Max bounces", ImGuiDataType_U32, &m_CameraControls.MaxBounces, &minBounces, &maxBounces);
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
    {
        CameraControls& c = m_CameraControls;
        changed |= ImGui::DragFloat3  ("Camera Position"       , &c.Position.x   , 0.05f);
        changed |= ImGui::DragFloat   ("Camera Yaw"            , &c.Yaw          , 0.05f);
        changed |= ImGui::DragFloat   ("Camera Pitch"          , &c.Pitch        , 0.05f, -89.0f, 89.0f);   // la ±90, „înainte” devine paralel cu „sus” și produsul vectorial dă zero
        changed |= ImGui::SliderFloat ("Camera FOV"            , &c.FovY         , +10.00f, 120.00f);
        changed |= ImGui::SliderFloat ("Camera F-Stop"         , &c.FStop        , 1.4f, 22.0f, "f/%.1f");
        changed |= ImGui::DragFloat   ("Camera Focus Distance" , &c.FocusDistance, 0.01f, 0.1f, 100.0f, "%.2f m");
        // Logarithmic: 1-2x and 50-100x are equally easy to reach.
        changed |= ImGui::SliderFloat ("Camera Bokeh Scale"    , &c.BokehScale   , 1.0f, 100.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::Checkbox    ("Depth of Field as Post-Process", &c.PostProcessDoF);
    }

    if (changed) m_SceneChanged = true;
}
#endif // #ifdef GRAM_IMGUI_ENABLED

#endif // #ifdef GRAM_VULKAN_RENDERER
