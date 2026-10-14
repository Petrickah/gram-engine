#include "Gram/EntryPoint.hpp"
#include "Gram/GLFWApplication.hpp"
#include "Gram/VulkanRenderer.hpp"

#include <iostream>

Gram::Application* Gram::CreateApplication(int argc, char** argv)
{
    Gram::ApplicationSpecification appSpec;
    appSpec.m_WindowSize  = glm::vec2(1280, 720);
    appSpec.m_WindowTitle = "Gram Application with Vulkan";
    
    Gram::RendererSpecification rendererSpec;
    rendererSpec.m_Application     = new Gram::GLFWApplication(appSpec);
    rendererSpec.m_ExtensionLayers = Application::GetRequiredInstanceExtensions();

    return rendererSpec.m_Application;
}
