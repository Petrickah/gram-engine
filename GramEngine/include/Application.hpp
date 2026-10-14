#pragma once

#include "glm/vec2.hpp"

#include "Renderer.hpp"

#include <string>

namespace Gram {
    struct ApplicationSpecification {
        glm::vec2 m_WindowSize = glm::vec2(800, 600);
        std::string m_WindowTitle = "Gram";
        Renderer* m_Renderer = nullptr;

        ~ApplicationSpecification()
        {
            // Renderer owns objects that depend on the window; tear it down first.
            if (m_Renderer) delete m_Renderer;
        }
    };

    class Application {
    public:
        Application(const ApplicationSpecification& appSpec)
        : m_AppSpec(appSpec) 
        {};
        
        virtual ~Application() {};
        virtual void Run() = 0;

        const ApplicationSpecification& GetAppSpec() const { return m_AppSpec; }

    protected:
        ApplicationSpecification m_AppSpec;
    };
}