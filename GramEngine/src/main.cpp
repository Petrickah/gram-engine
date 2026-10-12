#include "EntryPoint.hpp"

#include "VulkanRenderer.hpp"
#include "GLFWApplication.hpp"

#include <iostream>

Gram::Application* Gram::CreateApplication(int argc, char** argv) {
    Gram::ApplicationSpecification appSpec;
    appSpec.m_WindowSize  = glm::vec2(1280, 720);
    appSpec.m_WindowTitle = "Gram Application with Vulkan";

    return new Gram::GLFWApplication(appSpec, new Gram::VulkanRenderer);
}
