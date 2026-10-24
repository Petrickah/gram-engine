#include "Gram/EntryPoint.hpp"
#include "Gram/GLFWApplication.hpp"
#include "Gram/VulkanRenderer.hpp"

#ifdef GRAM_VULKAN_RENDERER
std::vector<const char*> extensionsLayers = {
    "VK_EXT_debug_utils",
    GramAppleOnly("VK_KHR_portability_enumeration",)
};

std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation",
};
#endif // #ifdef GRAM_VULKAN_RENDERER

Gram::Application* Gram::CreateApplication(int argc, char** argv)
{
    Gram::Application* app = nullptr;
    Gram::GLFWApplicationSpecification appSpec {
        .m_EnableLogging = true,
        .m_WindowSize  = glm::vec2(1280, 720),
        .m_WindowTitle = "Gram Application with Vulkan",
    };

    // Call into the logger entry point before handing off the application
    GramLoggerOnly(Gram::LoggerEntryPoint(appSpec.m_EnableLogging));

    app = new Gram::GLFWApplication(appSpec);

#ifdef GRAM_VULKAN_RENDERER
    Gram::VulkanRendererSpecification rendererSpec {
        // Enable custom debug messages, currently used to see which extensions and validations are available
        .m_DisplayDebug = true,
        
        // The window (and GLFW) must exist before the renderer can ask for instance extensions.
        .m_Application      = app,
        .m_ProjectWorkspace = Gram::GetExecutableDirectory(argc > 0 ? argv[0] : nullptr),
        .m_ExtensionLayers  = appSpec.GetRequiredInstanceExtensions(&rendererSpec, extensionsLayers.size(), extensionsLayers.data(), rendererSpec.m_DisplayDebug),
        .m_DeviceExtensions = appSpec.GetRequiredDeviceExtensions(&rendererSpec, deviceExtensions.size(), deviceExtensions.data(), rendererSpec.m_DisplayDebug),
        .m_ValidationLayers = appSpec.GetRequiredValidationLayers(&rendererSpec, validationLayers.size(), validationLayers.data(), rendererSpec.m_DisplayDebug),
    };

    // The application owns the renderer from here on.
    app->SetRenderer(new Gram::VulkanRenderer(&rendererSpec));
#endif // #ifdef GRAM_VULKAN_RENDERER

    return app;
}
