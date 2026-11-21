#include "Gram/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"
#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <algorithm>
#include <cstring>

#ifdef GRAM_IMGUI_ENABLED
#include <imgui.h>
#endif // #ifdef GRAM_IMGUI_ENABLED

GRAM_NAMESPACE_BEGIN

#ifdef GRAM_VULKAN_RENDERER
std::vector<std::string> GLFWApplicationSpecification::GetRequiredDeviceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug) const
{
    std::vector<std::string> requiredExtensions;
    if (extensions)
    {
        requiredExtensions.insert(requiredExtensions.end(), extensions, extensions + extensionCount);
    }

    return requiredExtensions;
}

std::vector<std::string> GLFWApplicationSpecification::GetRequiredInstanceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount, const char **extensions, bool displayDebug) const
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

std::vector<std::string> GLFWApplicationSpecification::GetRequiredValidationLayers(const VulkanRendererSpecification* rendererSpec, uint32_t validationCount, const char** validations, bool displayDebug) const
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

GLFWApplication::GLFWApplication(const GLFWApplicationSpecification &appSpec)
    : m_AppSpec(appSpec)
{
    GramVulkanOnly(glfwInitVulkanLoader(vkGetInstanceProcAddr));

    glfwSetErrorCallback([](int code, const char* msg) { GramError("GLFW error {}: {}", code, msg); });

    if (!glfwInit()) {
        GramError("GLFW: initialization failed");
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_GLFWmonitor = glfwGetPrimaryMonitor();
    if (!m_GLFWmonitor)
    {
        glfwTerminate();
        GramError("GLFW: primary monitor not found");
        std::exit(EXIT_FAILURE);
    }

    // Always, whatever the starting mode: Borderless needs it, and once Fullscreen changes the monitor's mode
    // glfwGetVideoMode reports the changed one.
    const GLFWvidmode* desktopMode = glfwGetVideoMode(m_GLFWmonitor);
    if (!desktopMode)
    {
        glfwTerminate();
        GramError("GLFW: couldn't read the primary monitor's video mode");
        std::exit(EXIT_FAILURE);
    }

    m_DesktopMode = {
        .Width       = desktopMode->width,
        .Height      = desktopMode->height,
        .RefreshRate = desktopMode->refreshRate
    };

    // Always created windowed, then SetWindowMode below: one code path for every mode, and a windowed
    // size and position to come back to.
    m_GLFWwindow = glfwCreateWindow(
        m_AppSpec.m_WindowSize.x,         // Window Width
        m_AppSpec.m_WindowSize.y,         // Window Height
        m_AppSpec.m_WindowTitle.c_str(),  // Window Title
        nullptr,
        nullptr
    );

    if (!m_GLFWwindow)
    {
        glfwTerminate();
        GramError("GLFW: window creation failed");
        std::exit(EXIT_FAILURE);
    }

    glfwGetWindowPos(m_GLFWwindow, &m_WindowSpecs.XPos, &m_WindowSpecs.YPos);
    glfwGetWindowSize(m_GLFWwindow, &m_WindowSpecs.Width, &m_WindowSpecs.Height);

    if (appSpec.m_EnableLogging)
    {
        int platform = glfwGetPlatform();
        GramInfo("GLFW Platform: {}",
            (platform == GLFW_PLATFORM_COCOA)   ? "Cocoa"   :
            (platform == GLFW_PLATFORM_WIN32)   ? "Win32"   :
            (platform == GLFW_PLATFORM_X11)     ? "X11"     :
            (platform == GLFW_PLATFORM_WAYLAND) ? "Wayland" : "Unknown"
        );

        GramInfo("GLFW Window Size: {}x{} units", m_WindowSpecs.Width, m_WindowSpecs.Height);
        GramInfo("GLFW Window Pos:  {}x{} units", m_WindowSpecs.XPos,  m_WindowSpecs.YPos);

        int width, height;
        glfwGetFramebufferSize(m_GLFWwindow, &width, &height);
        GramInfo("GLFW Framebuffer Size: {}x{} pixels", width, height);

        float xscale, yscale;
        glfwGetWindowContentScale(m_GLFWwindow, &xscale, &yscale);
        GramInfo("GLFW Content Scale: ({}, {})", xscale, yscale);

        GramInfo("GLFW Desktop Mode: {}x{}@{}", m_DesktopMode.Width, m_DesktopMode.Height, m_DesktopMode.RefreshRate);
        std::string modes;
        for (const GLFWApplicationVideoMode& mode : GetVideoModes())
        {
            modes += std::format("{}{}x{}@{}", modes.empty() ? "" : ", ", mode.Width, mode.Height, mode.RefreshRate);
        }
        GramInfo("GLFW Video Modes: {}", modes);
    }

    // Some platforms never report an out-of-date swapchain on resize, so tell the renderer directly.
    glfwSetWindowUserPointer(m_GLFWwindow, this);
    glfwSetFramebufferSizeCallback(m_GLFWwindow, [](GLFWwindow* window, int, int)
    {
        if (auto* application = static_cast<GLFWApplication*>(glfwGetWindowUserPointer(window)))
        {
            if (std::shared_ptr<VulkanRenderer> renderer = application->Get<VulkanRenderer>())
            {
                renderer->OnResize();
            }
        }
    });

    glfwSetKeyCallback(m_GLFWwindow, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
    {
        switch (key)
        {
        case GLFW_KEY_ESCAPE:
            if (action == GLFW_PRESS)
            {
                GramImGuiOnly(if (ImGui::GetIO().WantCaptureKeyboard) break;)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            break;
        default:
            break;
        }
    });

    // Before the renderer: the swapchain is then created at the final size.
    SetWindowMode(m_AppSpec.m_WindowMode, m_AppSpec.m_FullscreenMode);

    GramVulkanOnly(Gram::VulkanRendererSpecification vulkanSpecification(this, m_AppSpec));
    GramVulkanOnly(GramCheckVoid(RegisterLayer<Gram::VulkanRenderer>(m_VulkanRenderer, &vulkanSpecification), "couldn't initialize the Vulkan Renderer framework"));
}

GLFWApplication::~GLFWApplication()
{
    // Renderer owns Vulkan objects that depend on the window/GLFW; tear it down first.
    UnregisterAll();

    glfwDestroyWindow(m_GLFWwindow);
    glfwTerminate();
}

std::vector<GLFWApplicationVideoMode> GLFWApplication::GetVideoModes() const
{
    int32_t count = 0;
    std::vector<GLFWApplicationVideoMode> modes;
    const GLFWvidmode* videoModes = glfwGetVideoModes(m_GLFWmonitor, &count);
    
    modes.reserve(count);
    for (int32_t index = 0; index < count; index++)
    {
        auto found = std::find_if(modes.begin(), modes.end(), [&currMode = videoModes[index]](const GLFWApplicationVideoMode& videoMode)
        {
            return videoMode.Width == currMode.width && videoMode.Height == currMode.height && videoMode.RefreshRate == currMode.refreshRate;
        });

        if (found != modes.end()) continue;

        modes.push_back({
            .Width       = videoModes[index].width,
            .Height      = videoModes[index].height,
            .RefreshRate = videoModes[index].refreshRate
        });
    }

    return modes;
}

void GLFWApplication::SetWindowMode(GLFWApplicationWindowMode windowMode, GLFWApplicationVideoMode videoMode)
{
    const GLFWApplicationVideoMode desktopMode = GetDesktopMode();

    // An empty mode (or one never set) means the monitor's own: glfwSetWindowMonitor can't take 0x0.
    if (videoMode.Width <= 0 || videoMode.Height <= 0) videoMode = desktopMode;

    // Nothing to do when already there; Fullscreen to Fullscreen at another resolution is a real change.
    const bool sameVideoMode = videoMode.Width       == m_FullscreenMode.Width
                            && videoMode.Height      == m_FullscreenMode.Height
                            && videoMode.RefreshRate == m_FullscreenMode.RefreshRate;
    if (windowMode == m_WindowMode && (windowMode != GLFWApplicationWindowMode::Fullscreen || sameVideoMode)) return;

    // Only while windowed: from Borderless or Fullscreen it would save the screen's size.
    if (m_WindowMode == GLFWApplicationWindowMode::Windowed)
    {
        glfwGetWindowPos(m_GLFWwindow, &m_WindowSpecs.XPos, &m_WindowSpecs.YPos);
        glfwGetWindowSize(m_GLFWwindow, &m_WindowSpecs.Width, &m_WindowSpecs.Height);
    }

    m_WindowMode = windowMode;
    if (windowMode == GLFWApplicationWindowMode::Fullscreen) m_FullscreenMode = videoMode;

    switch (windowMode)
    {
    case GLFWApplicationWindowMode::Windowed:
        glfwSetWindowMonitor(
            m_GLFWwindow,
            nullptr,
            m_WindowSpecs.XPos,
            m_WindowSpecs.YPos,
            m_WindowSpecs.Width,
            m_WindowSpecs.Height,
            GLFW_DONT_CARE
        );
        break;
    case GLFWApplicationWindowMode::Borderless:
        glfwSetWindowMonitor(
            m_GLFWwindow,
            m_GLFWmonitor,
            0, 0,
            desktopMode.Width,
            desktopMode.Height,
            desktopMode.RefreshRate
        );
        break;
    case GLFWApplicationWindowMode::Fullscreen:
        glfwSetWindowMonitor(
            m_GLFWwindow,
            m_GLFWmonitor,
            0, 0,
            videoMode.Width,
            videoMode.Height,
            videoMode.RefreshRate
        );
        break;
    default:
        break;
    }
}

bool GLFWApplication::Run()
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

        if (!DrawFrame()) break;
    }

    return Finish();
}

GRAM_NAMESPACE_END