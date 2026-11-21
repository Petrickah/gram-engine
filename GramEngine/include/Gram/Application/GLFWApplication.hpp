#pragma once

GRAM_NAMESPACE_BEGIN

#ifdef GRAM_VULKAN_RENDERER
struct VulkanRenderer;
struct VulkanRendererSpecification;
#endif // #ifdef GRAM_VULKAN_RENDERER

enum class GLFWApplicationWindowMode
{
    Windowed, Borderless, Fullscreen
};

struct GLFWApplicationVideoMode
{
    int Width       = 0;
    int Height      = 0;
    int RefreshRate = 0;

    bool operator==(const GLFWApplicationVideoMode&) const = default;
};

struct GLFWApplicationSpecification
{
    bool                      m_EnableLogging = false;
    std::string               m_WindowTitle   = "Gram";
    std::filesystem::path     m_Workspace     = "";
    // How the window starts. Windowed: m_WindowSize. Fullscreen: m_FullscreenMode, the monitor's own mode when left empty.
    GLFWApplicationWindowMode m_WindowMode     = GLFWApplicationWindowMode::Windowed;
    glm::ivec2                m_WindowSize     = glm::ivec2(800, 600);
    GLFWApplicationVideoMode  m_FullscreenMode = {};

#ifdef GRAM_VULKAN_RENDERER
    std::vector<std::string> GetRequiredDeviceExtensions  (const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false) const;
    std::vector<std::string> GetRequiredInstanceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false) const;
    std::vector<std::string> GetRequiredValidationLayers  (const VulkanRendererSpecification* rendererSpec, uint32_t validationCount = 0, const char** validations = nullptr, bool displayDebug = false) const;
#endif // #ifdef GRAM_VULKAN_RENDERER
};

class GLFWApplication : public Gram::Application, public LayerOwner<GLFWApplication>
{
public:
    GLFWApplication(const GLFWApplicationSpecification& appSpec);

    virtual ~GLFWApplication() override;
    virtual bool Run() override;

    GLFWwindow* GetWindow() const { return m_GLFWwindow; }
    const GLFWApplicationSpecification& GetAppSpec() const { return m_AppSpec; }

    // The primary monitor's modes, without duplicates (GLFW lists one per color depth), smallest first.
    std::vector<GLFWApplicationVideoMode> GetVideoModes() const;
    // The monitor's own mode, read once at startup: while Fullscreen changes it, the monitor reports the changed one.
    GLFWApplicationVideoMode  GetDesktopMode()    const { return m_DesktopMode;    }
    GLFWApplicationWindowMode GetWindowMode()     const { return m_WindowMode;     }
    GLFWApplicationVideoMode  GetFullscreenMode() const { return m_FullscreenMode; }

    // videoMode is only read for Fullscreen; an empty one means the monitor's own mode.
    void SetWindowMode(GLFWApplicationWindowMode windowMode, GLFWApplicationVideoMode videoMode = {});

private:
    const GLFWApplicationSpecification m_AppSpec;
    GLFWmonitor* m_GLFWmonitor = nullptr;
    GLFWwindow*  m_GLFWwindow  = nullptr;

    // Where the window was while windowed, saved when it leaves Windowed and restored when it comes back.
    struct WindowSpecs
    {
        int Width  = 0;
        int Height = 0;
        int XPos   = 0;
        int YPos   = 0;
    };

    WindowSpecs               m_WindowSpecs;
    GLFWApplicationVideoMode  m_DesktopMode;
    GLFWApplicationWindowMode m_WindowMode     = GLFWApplicationWindowMode::Windowed;
    GLFWApplicationVideoMode  m_FullscreenMode = {};

protected:
    GramVulkanOnly(std::shared_ptr<Gram::VulkanRenderer> m_VulkanRenderer = nullptr);
};

GRAM_NAMESPACE_END