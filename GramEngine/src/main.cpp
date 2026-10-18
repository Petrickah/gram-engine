#include "Gram/EntryPoint.hpp"
#include "Gram/GLFWApplication.hpp"
#include "Gram/VulkanRenderer.hpp"

Gram::Application* Gram::CreateApplication(int argc, char** argv)
{
    Gram::ApplicationSpecification appSpec;
    appSpec.m_WindowSize  = glm::vec2(1280, 720);
    appSpec.m_WindowTitle = "Gram Application with Vulkan";

    std::vector<const char*> extensionsLayers = {
#ifdef GRAM_PLATFORM_APPLE
        "VK_KHR_portability_enumeration",
#endif // #ifdef GRAM_PLATFORM_APPLE
        "VK_EXT_debug_utils",
    };

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    
    std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };
    
    Gram::VulkanRendererSpecification rendererSpec;

    // Enable custom debug messages, currently used to see which extensions and validations are available
    rendererSpec.m_DisplayDebug     = true;

    // The window (and GLFW) must exist before the renderer can ask for instance extensions.
    rendererSpec.m_Application      = new Gram::GLFWApplication(appSpec);
    rendererSpec.m_ProjectWorkspace = std::filesystem::path(argv[0]).parent_path();
    rendererSpec.m_DeviceExtensions = appSpec.GetRequiredDeviceExtensions(&rendererSpec, deviceExtensions.size(), deviceExtensions.data(), rendererSpec.m_DisplayDebug);
    rendererSpec.m_ExtensionLayers  = appSpec.GetRequiredInstanceExtensions(&rendererSpec, extensionsLayers.size(), extensionsLayers.data(), rendererSpec.m_DisplayDebug);
    rendererSpec.m_ValidationLayers = appSpec.GetRequiredValidationLayers(&rendererSpec, validationLayers.size(), validationLayers.data(), rendererSpec.m_DisplayDebug);
    
    // The application owns the renderer from here on.
    rendererSpec.m_Application->SetRenderer(new Gram::VulkanRenderer(&rendererSpec));

    return rendererSpec.m_Application;
}
