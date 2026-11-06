#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"

#include "Gram/Application/GLFWApplication.hpp"

#ifdef GRAM_IMGUI_ENABLED
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui.h>

GRAM_NAMESPACE_BEGIN

VulkanImGui::VulkanImGui(
    const VulkanRendererSpecification *rendererSpec,
    const vk::Instance&                instance,
    const vk::PhysicalDevice&          physicalDevice,
    const vk::Device&                  device,
    const vk::SurfaceFormatKHR&        swapChainFormat,
    QueueFamilyIndices&                queueFamilyIndices,
    uint32_t                           imageCount
)
{
    // create descriptor pool for IMGUI
    // the size of the pool is very oversize, but it's copied from imgui demo itself.
    std::vector<vk::DescriptorPoolSize> poolSizes
    {
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eSampler             , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eSampledImage        , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eStorageImage        , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eUniformTexelBuffer  , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eStorageTexelBuffer  , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eUniformBuffer       , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eStorageBuffer       , .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eUniformBufferDynamic, .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eStorageBufferDynamic, .descriptorCount = 1000 },
        vk::DescriptorPoolSize { .type = vk::DescriptorType::eInputAttachment     , .descriptorCount = 1000 }
    };

    vk::DescriptorPoolCreateInfo poolInfo {
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = 1000,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data(),
    };

    vk::ResultValue<vk::UniqueDescriptorPool> result = device.createDescriptorPoolUnique(poolInfo);
    GramVulkanCheckVoid(result.result, "error creating the descriptor pool for ImGui");

    m_ImGuiPool = std::move(result.value);
    m_IniPath   = (rendererSpec->m_ProjectWorkspace / "imgui.ini").string();

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = m_IniPath.c_str();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable
                               |  ImGuiConfigFlags_ViewportsEnable
                               ;
    
    if (!queueFamilyIndices.IsComplete())
    {
        return;
    }
    const vk::Queue graphicsQueue = device.getQueue(*queueFamilyIndices.m_GraphicsComputeFamily, 0);
    
    //this initializes imgui for Vulkan
    const VkFormat vkSwapChainFormat = static_cast<VkFormat>(swapChainFormat.format);
    ImGui_ImplVulkan_InitInfo initInfo {
        .Instance            = static_cast<VkInstance>(instance),
        .PhysicalDevice      = static_cast<VkPhysicalDevice>(physicalDevice),
        .Device              = static_cast<VkDevice>(device),
        .QueueFamily         = *queueFamilyIndices.m_GraphicsComputeFamily,
        .Queue               = static_cast<VkQueue>(graphicsQueue),
        .DescriptorPool      = static_cast<VkDescriptorPool>(*m_ImGuiPool),
        .MinImageCount       = imageCount,
        .ImageCount          = imageCount,
        .PipelineInfoMain    = { 
            .PipelineRenderingCreateInfo = {
                .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                .colorAttachmentCount    = 1,
                .pColorAttachmentFormats = &vkSwapChainFormat
            }
        },
        .PipelineInfoForViewports =  {
            .PipelineRenderingCreateInfo = {
                .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                .colorAttachmentCount    = 1,
                .pColorAttachmentFormats = &vkSwapChainFormat
            }
        },
        .UseDynamicRendering = true,
    };

    GLFWApplication* app = dynamic_cast<GLFWApplication*>(rendererSpec->m_Application);
    if (!app)
    {
        GramError("ImGui: no GLFWApplication to attach to");
        return;
    }

    constexpr bool installCallbacks = true;
    if (!ImGui_ImplGlfw_InitForVulkan(app->GetWindow(), installCallbacks))
    {
        GramError("ImGui: GLFW backend initialization failed");
        return;
    }

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        GramError("ImGui: Vulkan backend initialization failed");
        return;
    }
}

bool VulkanImGui::Record(vk::CommandBuffer cmd, VulkanPass::Context ctx)
{
    // Loads what the passes before this one rendered (or clears, if none did): ImGui draws on top.
    BeginSwapchainRendering(ctx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    DrawFrame();
    ImGui::Render();
    
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
    cmd.endRendering();

    return true;
}

bool VulkanImGui::Finish()
{
    if (!super::Finish())
    {
        GramError("finishing all the layers failed");
    }
    
    UnregisterAll();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return true;
}

bool VulkanImGui::OnPresented()
{
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_IMGUI_ENABLED