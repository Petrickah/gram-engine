#include "Gram/GLFWApplication.hpp"

#include <cstdlib>
#include "Application.hpp"

GRAM_NAMESPACE_BEGIN

#ifdef GRAM_VULKAN_RENDERER
std::vector<std::string> GLFWApplicationSpecification::GetRequiredDeviceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug)
{
    std::vector<std::string> requiredExtensions;
    if (extensions)
    {
        requiredExtensions.insert(requiredExtensions.end(), extensions, extensions + extensionCount);
    }

    return requiredExtensions;
}

std::vector<std::string> GLFWApplicationSpecification::GetRequiredInstanceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug)
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
        GramError("GLFW: not all required instance extensions are supported");
    }

    return requiredExtensions;
}

std::vector<std::string> GLFWApplicationSpecification::GetRequiredValidationLayers(const VulkanRendererSpecification* rendererSpec, uint32_t validationCount, const char** validations, bool displayDebug)
{
    std::vector<std::string> requiredValidations;
    if (validations)
    {
        requiredValidations.assign(validations, validations + validationCount);
    }

    if (!rendererSpec->AreValidationsSupported(requiredValidations, displayDebug))
    {
        GramError("GLFW: not all requested validation layers are supported");
    }

    return requiredValidations;
}
#endif // #ifdef GRAM_VULKAN_RENDERER

GLFWApplication::GLFWApplication(const GLFWApplicationSpecification &appSpec) : m_AppSpec(appSpec)
{
#ifdef GRAM_VULKAN_RENDERER
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif // GRAM_VULKAN_RENDERER

    glfwSetErrorCallback([](int code, const char* msg) { GramError("GLFW error {}: {}", code, msg); });

    if (!glfwInit()) {
        GramError("GLFW: initialization failed");
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

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
        GramError("GLFW: window creation failed");
        std::exit(EXIT_FAILURE);
    }

    // Some platforms never report an out-of-date swapchain on resize, so tell the renderer directly.
    glfwSetWindowUserPointer(m_GLFWwindow, this);
    glfwSetFramebufferSizeCallback(m_GLFWwindow, [](GLFWwindow* window, int, int) {
        auto* application = static_cast<GLFWApplication*>(glfwGetWindowUserPointer(window));
        if (application && application->m_Renderer) application->m_Renderer->OnResize();
    });
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

        // Minimized: nothing to draw on, sleep until the next window event instead of spinning.
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_GLFWwindow, &width, &height);
        if (width == 0 || height == 0)
        {
            glfwWaitEvents();
            continue;
        }

        if (!m_Renderer->DrawFrame()) break;
    }

    m_Renderer->Finish();
}

GRAM_NAMESPACE_END