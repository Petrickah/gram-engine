#include "Gram/VulkanRenderer.hpp"

#include <iostream>

void Gram::VulkanRenderer::Init()
{
    if (!m_RendererSpec.m_Application)
    {
        std::cerr << "VulkanRenderer::Init called without an Application!\n";
        return;
    }

    const ApplicationSpecification& appSpec = m_RendererSpec.m_Application->GetAppSpec();

    vk::ApplicationInfo vkApplicationInfo(
        appSpec.m_WindowTitle.c_str(), // Application Title
        VK_MAKE_VERSION(1, 0, 0),      // Application Version
        "Gram Engine",                 // Engine Title
        VK_MAKE_VERSION(1, 0, 0),      // Engine Version
        VK_API_VERSION_1_0,            // Vulkan API Version
        nullptr
    );

    if (m_RendererSpec.m_ExtensionLayers.empty())
    {
        std::cerr << "GLFW reports no Vulkan support (no required instance extensions)!\n";
    }

    vk::InstanceCreateInfo vkInstanceCreateInfo(
        vk::InstanceCreateFlags(),
        &vkApplicationInfo, // ApplicationInfo
        0,                  // Layers Count
        nullptr,            // Layers Names
        static_cast<uint32_t>(m_RendererSpec.m_ExtensionLayers.size()), // Extensions Count
        m_RendererSpec.m_ExtensionLayers.data(),                        // Extensions Names
        nullptr
    );

    vk::Result vkResult = vk::createInstance(&vkInstanceCreateInfo, nullptr, &m_VkInstance);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "failed to create instance: " << vk::to_string(vkResult) << "\n";
    }
}

Gram::VulkanRenderer::~VulkanRenderer()
{
    if (m_VkInstance) m_VkInstance.destroy();
}
