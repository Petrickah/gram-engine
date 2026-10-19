#pragma once

#include "Common.hpp"

GRAM_NAMESPACE_BEGIN

class Application;

struct RendererSpecification
{
    bool m_DisplayDebug = false;
    std::filesystem::path m_ProjectWorkspace = "";
    Application* m_Application = nullptr;
    std::vector<std::string> m_ExtensionLayers;
    std::vector<std::string> m_DeviceExtensions;
    std::vector<std::string> m_ValidationLayers;

    virtual bool AreDeviceExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const = 0;
    virtual bool AreExtensionsSupported(const std::vector<std::string>& extensions, bool displayDebug = false) const = 0;
    virtual bool AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug = false) const = 0;

    // C-string views into the std::string members above (what Vulkan's create infos want).
    // They stay valid only while the members are unchanged, and the returned vector must be
    // kept alive for as long as its .data() is in use: never call .data() on the temporary.
    std::vector<const char*> GetExtensionLayers() const { return ToCStrings(m_ExtensionLayers); }
    std::vector<const char*> GetDeviceExtensions() const { return ToCStrings(m_DeviceExtensions); }
    std::vector<const char*> GetValidationLayers() const { return ToCStrings(m_ValidationLayers); }

private:
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

class Renderer
{
public:
    Renderer() = default;
    virtual ~Renderer() {};

public:
    virtual bool DrawFrame() = 0;
    virtual bool Finish() = 0;

    // The window's framebuffer changed size (including to or from zero when minimized).
    virtual void OnResize() {}
};

GRAM_NAMESPACE_END
