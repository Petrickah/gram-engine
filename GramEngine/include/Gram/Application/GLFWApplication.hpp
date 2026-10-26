#pragma once

GRAM_NAMESPACE_BEGIN

#ifdef GRAM_VULKAN_RENDERER
struct VulkanRendererSpecification;
#endif // #ifdef GRAM_VULKAN_RENDERER

struct GLFWApplicationSpecification
{
    bool        m_EnableLogging = false;
    glm::vec2   m_WindowSize    = glm::vec2(800, 600);
    std::string m_WindowTitle   = "Gram";
    
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

private:
    GLFWApplicationSpecification m_AppSpec;
    GLFWwindow* m_GLFWwindow;
};

GRAM_NAMESPACE_END