#pragma once

#include "Renderer.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"

GRAM_NAMESPACE_BEGIN

struct VulkanRendererSpecification: public RendererSpecification
{
    virtual bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const override;
};

class VulkanRenderer: public Renderer {
public:
    VulkanRenderer(const RendererSpecification* rendererSpec)
        : m_VulkanInstance(rendererSpec)
        , m_VulkanSurface(rendererSpec, m_VulkanInstance.GetInstance())
        , m_VulkanDevice(rendererSpec, m_VulkanInstance.GetInstance(), m_VulkanSurface.GetSurface())
    { }

private:
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
};

GRAM_NAMESPACE_END