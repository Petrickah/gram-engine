#include "Gram/EntryPoint.hpp"
#include "Gram/GLFWApplication.hpp"
#include "Gram/VulkanRenderer.hpp"

#include <iostream>

Gram::Application* Gram::CreateApplication(int argc, char** argv)
{
    Gram::ApplicationSpecification appSpec;
    appSpec.m_WindowSize  = glm::vec2(1280, 720);
    appSpec.m_WindowTitle = "Gram Application with Vulkan";

    std::vector<const char*> extensionsLayers = {
        "VK_EXT_debug_utils"
    };
    
    std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    
    Gram::RendererSpecification rendererSpec;

    // Enable custom debug messages, currently used to see which extensions and validations are available
    rendererSpec.m_DisplayDebug     = true;

    // The window (and GLFW) must exist before the renderer can ask for instance extensions.
    rendererSpec.m_Application      = new Gram::GLFWApplication(appSpec);
    rendererSpec.m_ExtensionLayers  = appSpec.GetRequiredInstanceExtensions(rendererSpec, extensionsLayers.size(), extensionsLayers.data(), rendererSpec.m_DisplayDebug);
    rendererSpec.m_ValidationLayers = appSpec.GetRequiredValidationLayers(rendererSpec, validationLayers.size(), validationLayers.data(), rendererSpec.m_DisplayDebug);
    
    // The application owns the renderer from here on.
    rendererSpec.m_Application->SetRenderer(new Gram::VulkanRenderer(rendererSpec));

    return rendererSpec.m_Application;
}
