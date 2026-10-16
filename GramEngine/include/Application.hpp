#pragma once

#include "Common.hpp"
#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

struct ApplicationSpecification
{
    glm::vec2 m_WindowSize = glm::vec2(800, 600);
    std::string m_WindowTitle = "Gram";

    std::vector<std::string> GetRequiredDeviceExtensions(const RendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false);
    std::vector<std::string> GetRequiredInstanceExtensions(const RendererSpecification* rendererSpec, uint32_t extensionCount = 0, const char** extensions = nullptr, bool displayDebug = false);
    std::vector<std::string> GetRequiredValidationLayers(const RendererSpecification* rendererSpec, uint32_t validationCount = 0, const char** validations = nullptr, bool displayDebug = false);
};

class Application
{
    public:
    Application(const ApplicationSpecification& appSpec)
        : m_AppSpec(appSpec) 
        {};
    
    // Derived classes delete the renderer in their own destructor, before the window goes away.
    virtual ~Application() { delete m_Renderer; };
    virtual void Run() = 0;

    // Takes ownership of the renderer.
    void SetRenderer(Renderer* renderer) { m_Renderer = renderer; }

    const ApplicationSpecification& GetAppSpec() const { return m_AppSpec; }

protected:
    ApplicationSpecification m_AppSpec;
    Renderer* m_Renderer = nullptr;
};

GRAM_NAMESPACE_END