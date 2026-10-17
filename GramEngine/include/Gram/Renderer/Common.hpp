#pragma once

#include "Common.hpp"
#include "Renderer.hpp"

#include <optional>
#include <vulkan/vulkan.hpp>

GRAM_NAMESPACE_BEGIN

struct VulkanRendererSpecification: public RendererSpecification
{
    virtual bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const override;
};

GRAM_NAMESPACE_END