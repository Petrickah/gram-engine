#pragma once

#include <vector>

namespace Gram
{
    class Application;

    struct RendererSpecification
    {
        bool m_DisplayDebug = false;
        Gram::Application* m_Application = nullptr;
        std::vector<const char*> m_ExtensionLayers;
        std::vector<const char*> m_ValidationLayers;

        bool AreExtensionsSupported(const std::vector<const char*>& extensions, bool displayDebug = false) const;
        bool AreValidationsSupported(const std::vector<const char*>& validations, bool displayDebug = false) const;
    };

    class Renderer {
    public:
        Renderer(const RendererSpecification& rendererSpec) : m_RendererSpec(rendererSpec) {}
        virtual ~Renderer() {};

    protected:
        RendererSpecification m_RendererSpec;
    };
}