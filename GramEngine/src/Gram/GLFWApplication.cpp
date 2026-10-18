#include "Gram/GLFWApplication.hpp"

#include <cstdlib>
#include <iostream>
#include "Application.hpp"

GRAM_NAMESPACE_BEGIN

std::vector<std::string> ApplicationSpecification::GetRequiredDeviceExtensions(const RendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug)
{
    std::vector<std::string> requiredExtensions;
    if (extensions)
    {
        requiredExtensions.insert(requiredExtensions.end(), extensions, extensions + extensionCount);
    }

    return requiredExtensions;
}

std::vector<std::string> ApplicationSpecification::GetRequiredInstanceExtensions(const RendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug)
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<std::string> requiredExtensions;
    if (glfwExtensions)
    {
        requiredExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    if (extensions)
    {
        requiredExtensions.insert(requiredExtensions.end(), extensions, extensions + extensionCount);
    }

    if (!rendererSpec->AreExtensionsSupported(requiredExtensions, displayDebug))
    {
        std::cout << "[GLFW] no required instance extensions!\n";
    }

    return requiredExtensions;
}

std::vector<std::string> ApplicationSpecification::GetRequiredValidationLayers(const RendererSpecification* rendererSpec, uint32_t validationCount, const char** validations, bool displayDebug)
{
    std::vector<std::string> requiredValidations;
    if (validations)
    {
        requiredValidations.assign(validations, validations + validationCount);
    }

    if (!rendererSpec->AreValidationsSupported(requiredValidations, displayDebug))
    {
        std::cout << "[GLFW] no validation layers!\n";
    }

    return requiredValidations;
}

GLFWApplication::GLFWApplication(const ApplicationSpecification &appSpec) : Application(appSpec)
{
#ifdef GRAM_VULKAN_RENDERER
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif // GRAM_VULKAN_RENDERER

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

GLFWApplication::~GLFWApplication()
{
    // Renderer owns Vulkan objects that depend on the window/GLFW; tear it down first.
    delete m_Renderer;
    m_Renderer = nullptr;

    glfwDestroyWindow(m_GLFWwindow);
    glfwTerminate();
}

void GLFWApplication::Run()
{
    while (!glfwWindowShouldClose(m_GLFWwindow))
    {
        glfwPollEvents();
    }
}

GRAM_NAMESPACE_END