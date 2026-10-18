#pragma once

#include "../Application.hpp"
#include <GLFW/glfw3.h>

namespace Gram
{
    class GLFWApplication : public Application
    {
    public:
        GLFWApplication(const Gram::ApplicationSpecification& appSpec);
    
        virtual ~GLFWApplication() override;
        virtual void Run() override;

        GLFWwindow* GetWindow() const { return m_GLFWwindow; }

    private:
        GLFWwindow* m_GLFWwindow;
    };
}