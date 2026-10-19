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