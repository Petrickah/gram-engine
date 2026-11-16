#pragma once

#include "Gram/Common.hpp"
#include "Gram/Application/GLFWApplication.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class GLFWApplication;

#ifdef GRAM_PLATFORM_APPLE
// Offered only by non-conformant drivers (MoltenVK); must be enabled whenever it's offered.
inline constexpr const char* vkPortabilitySubsetExtension = "VK_KHR_portability_subset";
#endif // #ifdef GRAM_PLATFORM_APPLE

// The max number of frames that can be rendered at a time;
// One active render while the other is presented on the screen.
inline constexpr uint32_t vkMaxFramesInFlight = 2;

// ImGui's backend (1.92.9b docking) transitions secondary-viewport swapchain
// images without acquiring them first; not ours to fix.
inline constexpr const char* vkNonAcquiredSwapchainImageUsed = "UNASSIGNED-non-acquired-swapchain-image-used";

struct VulkanRendererSpecification
{
    bool                                     m_DisplayDebug     = false;
    Gram::Application*                       m_Application      = nullptr;
    std::filesystem::path                    m_ProjectWorkspace = "";
    std::vector<std::string>                 m_ExtensionLayers  = {};
    std::vector<std::string>                 m_DeviceExtensions = {};
    std::vector<std::string>                 m_ValidationLayers = {};
    
    VulkanRendererSpecification(Gram::Application* application, const Gram::GLFWApplicationSpecification& spec)
        : m_Application(application)
        , m_ProjectWorkspace(spec.m_Workspace)
        , m_DisplayDebug(spec.m_EnableLogging)
    {
        m_ExtensionLayers  = spec.GetRequiredInstanceExtensions(this, static_cast<uint32_t>(m_RequiredExtensionsLayers.size()), m_RequiredExtensionsLayers.data(), m_DisplayDebug);
        m_DeviceExtensions = spec.GetRequiredDeviceExtensions  (this, static_cast<uint32_t>(m_RequiredDeviceExtensions.size()), m_RequiredDeviceExtensions.data(), m_DisplayDebug);
        m_ValidationLayers = spec.GetRequiredValidationLayers  (this, static_cast<uint32_t>(m_RequiredValidationLayers.size()), m_RequiredValidationLayers.data(), m_DisplayDebug);
    }

    bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const;
    bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const;
    bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const;

    // C-string views into the std::string members above (what Vulkan's create infos want).
    // They stay valid only while the members are unchanged, and the returned vector must be
    // kept alive for as long as its .data() is in use: never call .data() on the temporary.
    std::vector<const char*> GetExtensionLayers()  const { return ToCStrings(m_ExtensionLayers);  }
    std::vector<const char*> GetDeviceExtensions() const { return ToCStrings(m_DeviceExtensions); }
    std::vector<const char*> GetValidationLayers() const { return ToCStrings(m_ValidationLayers); }

private:
    std::vector<const char*> m_RequiredExtensionsLayers = { GramAppleOnly("VK_KHR_portability_enumeration",) "VK_EXT_debug_utils" };
    std::vector<const char*> m_RequiredDeviceExtensions = { "VK_KHR_swapchain" };
    std::vector<const char*> m_RequiredValidationLayers = { GramValidationOnly("VK_LAYER_KHRONOS_validation") };

protected:
    static std::vector<const char*> ToCStrings(const std::vector<std::string>& strings)
    {
        std::vector<const char*> cStrings;
        cStrings.reserve(strings.size());
        for (const std::string& string: strings)
        {
            cStrings.push_back(string.c_str());
        }

        return cStrings;
    }
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
