#pragma once

#include <GLFW/glfw3.h>

#include "Gram/Application.hpp"

#ifdef GRAM_VULKAN_RENDERER
#include "Gram/Renderer/VulkanRenderer.hpp"
#endif // #ifdef GRAM_VULKAN_RENDERER

GRAM_NAMESPACE_BEGIN

struct GLFWApplicationSpecification
{
    bool m_EnableLogging      = false;
    glm::vec2 m_WindowSize    = glm::vec2(800, 600);
    std::string m_WindowTitle = "Gram";

#ifdef GRAM_VULKAN_RENDERER
    std::vector<std::string> GetRequiredDeviceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false);
    std::vector<std::string> GetRequiredInstanceExtensions(const VulkanRendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false);
    std::vector<std::string> GetRequiredValidationLayers(const VulkanRendererSpecification* rendererSpec, uint32_t validationCount = 0, const char** validations = nullptr, bool displayDebug = false);
#endif // #ifdef GRAM_VULKAN_RENDERER
};

class GLFWApplication : public Application
{
public:
    GLFWApplication(const GLFWApplicationSpecification& appSpec);

    virtual ~GLFWApplication() override;
    virtual void Run() override;

    GLFWwindow* GetWindow() const { return m_GLFWwindow; }
    const GLFWApplicationSpecification& GetAppSpec() const { return m_AppSpec; }

private:
    GLFWApplicationSpecification m_AppSpec;
    GLFWwindow* m_GLFWwindow;
};

GRAM_NAMESPACE_END