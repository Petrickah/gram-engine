#pragma once

#include "glm/vec2.hpp"
#include "Renderer.hpp"

#include <string>

namespace Gram {
    struct ApplicationSpecification {
        glm::vec2 m_WindowSize = glm::vec2(800, 600);
        std::string m_WindowTitle = "Gram";
    };

    class Application {
    public:
        Application(const ApplicationSpecification& appSpec, Renderer* renderer = nullptr)
            : m_AppSpec(appSpec)
            , m_Renderer(renderer) {
            if (m_Renderer) m_Renderer->SetApplication(this);
        }

        virtual ~Application() {
            delete m_Renderer;
        };

        virtual void Run() = 0;
        virtual const char** GetExtensions(uint32_t& extensionCount) const = 0;
        
        const ApplicationSpecification& GetAppSpec() const { return m_AppSpec; }
    protected:
        Renderer* m_Renderer;
    private:
        ApplicationSpecification m_AppSpec;
    };
}