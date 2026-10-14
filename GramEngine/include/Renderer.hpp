#pragma once

#include <vector>

namespace Gram
{
    class Application;

    struct RendererSpecification
    {
        Gram::Application* m_Application = nullptr;
        std::vector<const char*> m_ExtensionLayers;
        std::vector<const char*> m_ValidationLayers;

        bool AreExtensionsSupported(const std::vector<const char*>& extensions) const;
    };

    class Renderer {
    public:
        Renderer(const RendererSpecification& rendererSpec) : m_RendererSpec(rendererSpec) {}
        virtual ~Renderer() {};

        // Called by the Application once it is fully constructed (window + GLFW up).
        virtual void Init() = 0;
    
    protected:
        const RendererSpecification& m_RendererSpec;
    };
}