#include "VulkanRenderer.hpp"

#include <iostream>

void Gram::VulkanRenderer::Init() {
    const Application* app = GetApplication();
    if (!app) {
        std::cerr << "VulkanRenderer::Init called without an Application!\n";
        return;
    }

    vk::ApplicationInfo vkApplicationInfo(
        app->GetAppSpec().m_WindowTitle.c_str(), // Application Title
        VK_MAKE_VERSION(1, 0, 0),      // Application Version
        "Gram Engine",                 // Engine Title
        VK_MAKE_VERSION(1, 0, 0),      // Engine Version
        VK_API_VERSION_1_0,            // Vulkan API Version
        nullptr
    );

    uint32_t extensionsCount = 0;
    const char** extensions = app->GetExtensions(extensionsCount);
    if (!extensions) {
        std::cerr << "GLFW reports no Vulkan support (no required instance extensions)!\n";
        return;
    }

    vk::InstanceCreateInfo vkInstanceCreateInfo(
        vk::InstanceCreateFlags(),
        &vkApplicationInfo, // ApplicationInfo
        0,                  // Layers Count
        nullptr,            // Layers Names
        extensionsCount,    // Extensions Count
        extensions,         // Extensions Names
        nullptr
    );

    vk::Result vkResult = vk::createInstance(&vkInstanceCreateInfo, nullptr, &m_VkInstance);
    if (vkResult != vk::Result::eSuccess) {
        std::cerr << "failed to create instance: " << vk::to_string(vkResult) << "\n";
    }
}

Gram::VulkanRenderer::~VulkanRenderer() {
    if (m_VkInstance) m_VkInstance.destroy();
}
