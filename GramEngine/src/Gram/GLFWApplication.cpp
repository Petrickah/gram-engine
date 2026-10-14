#include "Gram/GLFWApplication.hpp"

#include <cstdlib>
#include <iostream>

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
    uint32_t glfwExtensionsCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
    if (appSpec.m_Renderer) appSpec.m_Renderer->Init(glfwExtensionsCount, glfwExtensions);
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
