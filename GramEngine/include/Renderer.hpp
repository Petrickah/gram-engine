#pragma once

namespace Gram {
    class Application;

    class Renderer {
    public:
        Renderer() : m_Application(nullptr) {}
        virtual ~Renderer() {};

        // Called by the Application once it is fully constructed (window + GLFW up).
        virtual void Init() = 0;

        void SetApplication(const Application* app = nullptr) { m_Application = app; }
        const Application* GetApplication() const { return m_Application; }
    
    private:
        const Application* m_Application;
    };
}