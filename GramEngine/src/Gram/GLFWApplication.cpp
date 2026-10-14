#include "Gram/GLFWApplication.hpp"

#include <cstdlib>
#include <iostream>

std::vector<const char*> Gram::ApplicationSpecification::GetRequiredInstanceExtensions(const RendererSpecification& rendererSpec, uint32_t extensionCount, const char** extensions, bool displayDebug)
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> requiredExtensions;
    if (glfwExtensions)
    {
        requiredExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    if (extensions)
    {
        requiredExtensions.insert(requiredExtensions.end(), extensions, extensions + extensionCount);
    }

    if (!rendererSpec.AreExtensionsSupported(requiredExtensions, displayDebug))
    {
        std::cout << "[GLFW] no required instance extensions!\n";
    }

    return requiredExtensions;
}

std::vector<const char*> Gram::ApplicationSpecification::GetRequiredValidationLayers(const RendererSpecification& rendererSpec, uint32_t validationCount, const char** validations, bool displayDebug)
{
    std::vector<const char*> requiredValidations;
    if (validations)
    {
        requiredValidations.assign(validations, validations + validationCount);
    }

    if (!rendererSpec.AreValidationsSupported(requiredValidations, displayDebug))
    {
        std::cout << "[GLFW] no validation layers!\n";
    }

    return requiredValidations;
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
}

Gram::GLFWApplication::~GLFWApplication()
{
    // Renderer owns Vulkan objects that depend on the window/GLFW; tear it down first.
    delete m_Renderer;
    m_Renderer = nullptr;

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
