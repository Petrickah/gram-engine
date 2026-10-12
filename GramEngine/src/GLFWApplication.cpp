#include "GLFWApplication.hpp"

#include <cstdlib>
#include <iostream>

Gram::GLFWApplication::GLFWApplication(const Gram::ApplicationSpecification &appSpec, Renderer* renderer)
    : Gram::Application(appSpec, renderer)
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
    if (!m_GLFWwindow) {
        std::cerr << "failed to create GLFW window!\n";
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }

    // Window and GLFW are up: the renderer can now query extensions etc.
    if (m_Renderer) m_Renderer->Init();
}

Gram::GLFWApplication::~GLFWApplication()
{
    // Renderer owns Vulkan objects that depend on the window/GLFW; tear it down first.
    delete m_Renderer;
    m_Renderer = nullptr;

    glfwDestroyWindow(m_GLFWwindow);
    glfwTerminate();
}

void Gram::GLFWApplication::Run() {
    while (!glfwWindowShouldClose(m_GLFWwindow)) {
        glfwPollEvents();
    }
}
