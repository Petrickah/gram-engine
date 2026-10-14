#include "Gram/VulkanRenderer.hpp"

#include <iostream>

bool Gram::RendererSpecification::AreExtensionsSupported(const std::vector<const char*>& extensions) const
{
    vk::Result vkResult = vk::Result::eSuccess;

    uint32_t extensionCount = 0;
    vkResult = vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] reports no instance extensions: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    std::vector<vk::ExtensionProperties> supported(extensionCount);
    vkResult = vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, supported.data());

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] couldn't find instance extensions: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    bool allSupported = false;
    std::cout << "[Vulkan] available extensions:\n";

    for (const vk::ExtensionProperties& ext : supported)
    {
        bool found = std::any_of(extensions.begin(), extensions.end(), [ext](const char* name) {
            return std::strcmp(ext.extensionName.data(), name) == 0;
        });

        if (found) allSupported = true;
        if (found) std::cout << "     [X] " << ext.extensionName << '\n';
        else       std::cout << "     [-] " << ext.extensionName << '\n';
    }

    return allSupported;
}

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
