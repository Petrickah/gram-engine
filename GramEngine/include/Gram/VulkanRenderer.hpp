#pragma once

#include "Renderer.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanRenderer: public Renderer {
public:
    VulkanRenderer(const RendererSpecification* rendererSpec)
        : m_VulkanRendererSpec(
            dynamic_cast<const VulkanRendererSpecification*>(rendererSpec)
        )
        , m_VulkanInstance(
            m_VulkanRendererSpec
        )
        , m_VulkanSurface(
            m_VulkanRendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
        )
        , m_VulkanDevice(
            m_VulkanRendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface())
        )
        , m_VulkanSwapchain(
            m_VulkanRendererSpec,
            m_VulkanDevice.GetQueueFamilyIndices(),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface()),
            m_VulkanDevice.AreExtensionsSupported(m_VulkanRendererSpec)
        )
    { }

private:
    const VulkanRendererSpecification* m_VulkanRendererSpec;
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
    VulkanSwapchain m_VulkanSwapchain;
};

GRAM_NAMESPACE_END