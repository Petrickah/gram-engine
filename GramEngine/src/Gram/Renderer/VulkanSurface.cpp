#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"

#include "Gram/Application/GLFWApplication.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanSurface::VulkanSurface(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance)
    : m_VkInstance(static_cast<VkInstance>(instance))
{
    if (!rendererSpec || !m_VkInstance) return;
    
    if (!CreateSurfaceKHR(rendererSpec))
    {
        GramVulkanError("couldn't create a surface");
        m_VkSurface = VK_NULL_HANDLE;
    }
}

VulkanSurface::~VulkanSurface()
{
    if (m_VkSurface) DestroySurfaceKHR();
}

bool VulkanSurface::CreateSurfaceKHR(const VulkanRendererSpecification* rendererSpec)
{
    m_VkSurface = VK_NULL_HANDLE;
    GLFWApplication* application = rendererSpec ? dynamic_cast<GLFWApplication*>(rendererSpec->m_Application.get()) : nullptr;

    if (application == nullptr)
    {
        GramVulkanError("this is not a GLFWApplication");
        return false;
    }

    GramVulkanCheck(static_cast<vk::Result>(glfwCreateWindowSurface(m_VkInstance, application->GetWindow(), nullptr, &m_VkSurface)), "couldn't create a surface from GLFW");

    return true;
}

bool VulkanSurface::DestroySurfaceKHR()
{
    vkDestroySurfaceKHR(m_VkInstance, m_VkSurface, nullptr);
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER