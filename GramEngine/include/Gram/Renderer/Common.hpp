#pragma once

#include "Gram/Common.hpp"
#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Renderer.hpp"

#ifdef GRAM_VULKAN_RENDERER

#include <optional>
#include <vulkan/vulkan.hpp>

#ifdef GRAM_IMGUI_ENABLED
#include <imgui.h>
#endif // #ifdef GRAM_IMGUI_ENABLED

GRAM_NAMESPACE_BEGIN

class Application;

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
    bool                     m_DisplayDebug     = false;
    Application*             m_Application      = nullptr;
    std::filesystem::path    m_ProjectWorkspace = "";
    std::vector<std::string> m_ExtensionLayers;
    std::vector<std::string> m_DeviceExtensions;
    std::vector<std::string> m_ValidationLayers;
    
    bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const;
    bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const;
    bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const;

    // C-string views into the std::string members above (what Vulkan's create infos want).
    // They stay valid only while the members are unchanged, and the returned vector must be
    // kept alive for as long as its .data() is in use: never call .data() on the temporary.
    std::vector<const char*> GetExtensionLayers() const { return ToCStrings(m_ExtensionLayers); }
    std::vector<const char*> GetDeviceExtensions() const { return ToCStrings(m_DeviceExtensions); }
    std::vector<const char*> GetValidationLayers() const { return ToCStrings(m_ValidationLayers); }

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

// Owns Vulkan resources at a fixed address (members of VulkanRenderer): neither copyable nor
// movable. Declaring the copy operations, even deleted, also suppresses the implicit moves.
struct UniqueObject
{
    UniqueObject() = default;
    UniqueObject(const UniqueObject&) = delete;
    UniqueObject& operator=(const UniqueObject&) = delete;
};

// Move-only. The defaulted moves are only safe when every member owns itself (vk::Unique*
// handles, std containers) — a raw vk:: handle would be copied and then destroyed twice.
// Don't combine with UniqueObject: a derived class can only move if all its bases can.
struct MoveObject
{
    MoveObject() = default;
    MoveObject(const MoveObject&) = delete;
    MoveObject& operator=(const MoveObject&) = delete;
    MoveObject(MoveObject&&) noexcept = default;
    MoveObject& operator=(MoveObject&&) noexcept = default;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER