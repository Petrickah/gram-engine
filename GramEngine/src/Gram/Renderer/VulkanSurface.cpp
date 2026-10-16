#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/GLFWApplication.hpp"

GRAM_NAMESPACE_BEGIN

VulkanSurface::VulkanSurface(const RendererSpecification* rendererSpec, const vk::Instance& instance)
    : m_VkInstance(static_cast<VkInstance>(instance))
{
    if (rendererSpec && CreateSurfaceKHR(rendererSpec))
    {
        return;
    }

    std::cerr << "[Vulkan] couldn't create a surface!" << "\n";
    m_VkSurface = VK_NULL_HANDLE;
}

VulkanSurface::~VulkanSurface()
{
    if (m_VkSurface) DestroySurfaceKHR();
}

bool VulkanSurface::CreateSurfaceKHR(const RendererSpecification* rendererSpec)
{
    m_VkSurface = VK_NULL_HANDLE;
    GLFWApplication* application = rendererSpec ? dynamic_cast<GLFWApplication*>(rendererSpec->m_Application) : nullptr;

    if (application == nullptr)
    {
        std::cerr << "[Vulkan] this is not a GLFWApplication!\n";
        return false;
    }

    if (glfwCreateWindowSurface(m_VkInstance, application->GetWindow(), nullptr, &m_VkSurface) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] couldn't create a surface from GLFW!\n";
        return false;
    }

    return true;
}

bool VulkanSurface::DestroySurfaceKHR()
{
    vkDestroySurfaceKHR(m_VkInstance, m_VkSurface, nullptr);
    return true;
}

GRAM_NAMESPACE_END

