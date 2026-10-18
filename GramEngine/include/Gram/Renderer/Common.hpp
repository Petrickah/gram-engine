#pragma once

#include "Common.hpp"
#include "Renderer.hpp"

#include <optional>
#include <vulkan/vulkan.hpp>

GRAM_NAMESPACE_BEGIN

#ifdef GRAM_PLATFORM_APPLE
// Offered only by non-conformant drivers (MoltenVK); must be enabled whenever it's offered.
inline constexpr const char* vkPortabilitySubsetExtension = "VK_KHR_portability_subset";
#endif // #ifdef GRAM_PLATFORM_APPLE

struct VulkanRendererSpecification: public RendererSpecification
{
    virtual bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const override;
    virtual bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const override;
};

GRAM_NAMESPACE_END