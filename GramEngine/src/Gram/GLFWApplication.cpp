#include "Gram/GLFWApplication.hpp"

#include <cstdlib>
#include <iostream>

std::vector<const char*> Gram::Application::GetRequiredInstanceExtensions(const RendererSpecification& rendererSpec)
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> requiredExtensions;
    for (int i = 0; i < glfwExtensionCount; i++)
    {
        requiredExtensions.push_back(glfwExtensions[i]);
    }

    requiredExtensions.resize(glfwExtensionCount);
    if (!rendererSpec.AreExtensionsSupported(requiredExtensions))
    {
        std::cout << "GLFW reports no Vulkan support (no required instance extensions)!\n";
    }

    return requiredExtensions;
}

Gram::GLFWApplication::GLFWApplication(const Gram::ApplicationSpecification &appSpec) : Gram::Application(appSpec)
{
    glfwSetErrorCallback([](int code, const char* msg) { std::cerr << "GLFW error " << code << ": " << msg << "\n"; });

    if (!glfwInit()) {
        std::cerr << "failed to initialize GLFW!\n";
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_GLFWwindow = glfwCreateWindow(
        appSpec.m_WindowSize.x,         // Window Width
        appSpec.m_WindowSize.y,         // Window Height
        appSpec.m_WindowTitle.c_str(),  // Window Title
        nullptr,
        nullptr
    );

    if (!m_GLFWwindow)
    {
        glfwTerminate();
        std::cerr << "failed to create GLFW window!\n";
        std::exit(EXIT_FAILURE);
    }

    // Window and GLFW are up: the renderer can now query extensions etc.
    if (appSpec.m_Renderer) appSpec.m_Renderer->Init();
}

Gram::GLFWApplication::~GLFWApplication()
{
    glfwDestroyWindow(m_GLFWwindow);
    glfwTerminate();
}

void Gram::GLFWApplication::Run()
{
    while (!glfwWindowShouldClose(m_GLFWwindow))
    {
        glfwPollEvents();
    }
}
