#pragma once

#include "../Application.hpp"

// GLFW_INCLUDE_VULKAN only adds <vulkan/vulkan.h>; it does not stop glfw3.h
// from also pulling in <GL/gl.h>. GLFW_INCLUDE_NONE does.
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Gram {
    class GLFWApplication : public Application {
    public:
        GLFWApplication(const Gram::ApplicationSpecification& appSpec);
    
        virtual ~GLFWApplication() override;
        virtual void Run() override;

    private:
        GLFWwindow* m_GLFWwindow;
    };
}