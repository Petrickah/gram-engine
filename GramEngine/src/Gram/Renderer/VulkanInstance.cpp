#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/VulkanRenderer.hpp"
#include "Application.hpp"

GRAM_NAMESPACE_BEGIN

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              messageTypes,
                                                      vk::DebugUtilsMessengerCallbackDataEXT const * pCallbackData,
                                                      void *                                         pUserData)
{
    std::cout << "[Vulkan] validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

bool VulkanRendererSpecification::AreExtensionsSupported(const std::vector<std::string> &extensions, bool displayDebug) const
{
    vk::Result vkResult = vk::Result::eSuccess;

    uint32_t extensionCount = 0;
    vkResult = vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] reports no instance extensions: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    std::vector<vk::ExtensionProperties> supported(extensionCount);
    vkResult = vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, supported.data());

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] couldn't find instance extensions: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    if (displayDebug)
    {
        std::cout << "[Vulkan] available extensions:\n";
    }
    
    if (displayDebug)
    {
        for (const vk::ExtensionProperties& ext : supported)
        {
            bool requested = std::any_of(extensions.begin(), extensions.end(), [&ext](const std::string& name) {
                return name == ext.extensionName.data();
            });

            std::cout << (requested ? "    - [X] " : "    - [-] ") << ext.extensionName << '\n';
        }
    }

    // Every requested extension must be offered, not just one of them.
    bool allSupported = true;
    for (const std::string& name : extensions)
    {
        bool found = std::any_of(supported.begin(), supported.end(), [name](const vk::ExtensionProperties& ext) {
            return name == ext.extensionName.data();
        });

        if (!found)
        {
            std::cerr << "[Vulkan] instance extension not supported: " << name << "\n";
            allSupported = false;
        }
    }

    return allSupported;
}

bool VulkanRendererSpecification::AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug) const
{
    vk::Result vkResult = vk::Result::eSuccess;

    uint32_t layerCount;
    vkResult = vk::enumerateInstanceLayerProperties(&layerCount, nullptr);

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] reports no validation layers: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    std::vector<vk::LayerProperties> availableLayers(layerCount);
    vkResult = vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] couldn't find validation layers: " << vk::to_string(vkResult) << "!\n";
        return false;
    }

    if (displayDebug)
    {
        std::cout << "[Vulkan] available validations:\n";
    }
    
    if (displayDebug)
    {
        for (const vk::LayerProperties& layer : availableLayers)
        {
            bool requested = std::any_of(validations.begin(), validations.end(), [&layer](const std::string& name) {
                return name == layer.layerName.data();
            });

            std::cout << (requested ? "   - [X] " : "   - [-] ") << layer.layerName << '\n';
        }
    }

    // Every requested layer must be offered, not just one of them.
    bool allSupported = true;
    for (const std::string& name : validations)
    {
        bool found = std::any_of(availableLayers.begin(), availableLayers.end(), [name](const vk::LayerProperties& layer) {
            return name == layer.layerName.data();
        });

        if (!found)
        {
            std::cerr << "[Vulkan] validation layer not supported: " << name << "\n";
            allSupported = false;
        }
    }

    return allSupported;
}

VulkanInstance::~VulkanInstance()
{
    if (m_VkDebugMessenger) DestroyDebugUtilsMessengerExt();
    if (m_VkInstance) m_VkInstance.destroy();
}

VulkanInstance::VulkanInstance(const VulkanRendererSpecification* rendererSpec)
{
    if (!rendererSpec || !rendererSpec->m_Application)
    {
        std::cerr << "[Vulkan] created without an Application!\n";
        return;
    }

    const ApplicationSpecification& appSpec = rendererSpec->m_Application->GetAppSpec();

    vk::ApplicationInfo vkApplicationInfo(
        appSpec.m_WindowTitle.c_str(), // Application Title
        VK_MAKE_VERSION(1, 0, 0),      // Application Version
        "Gram Engine",                 // Engine Title
        VK_MAKE_VERSION(1, 0, 0),      // Engine Version
        VK_API_VERSION_1_1,            // Vulkan API Version
        nullptr
    );

    // The create info only stores pointers, so these vectors must outlive createInstance.
    const std::vector<const char*> validationLayers = rendererSpec->GetValidationLayers();
    const std::vector<const char*> extensionLayers  = rendererSpec->GetExtensionLayers();

    // Only ask for layers (and the messenger) if the driver actually offers all of them;
    // otherwise createInstance fails with eErrorLayerNotPresent.
    const bool useValidation = !validationLayers.empty()
                            && rendererSpec->AreValidationsSupported(rendererSpec->m_ValidationLayers);

    // Only ask for extensions if the driver actually offers all of them;
    const bool useExtensions = !extensionLayers.empty()
                            && rendererSpec->AreExtensionsSupported(rendererSpec->m_ExtensionLayers);

    // Chained into the instance create info so the layers also report problems from createInstance/destroy.
    const void* instancePNext = nullptr;
    if (useValidation)
    {
        PopulateDebugMessengerCreateInfo();
        instancePNext = &m_VkDebugMessengerCreateInfo;
    }

    
    vk::InstanceCreateInfo vkInstanceCreateInfo(
#ifdef GRAM_PLATFORM_APPLE
        vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#else
        vk::InstanceCreateFlags(),
#endif // #ifdef GRAM_PLATFORM_APPLE
        &vkApplicationInfo, // ApplicationInfo
        useValidation ? static_cast<uint32_t>(validationLayers.size()) : 0, // Layers Count
        useValidation ? validationLayers.data() : nullptr,                  // Layers Names
        useExtensions ? static_cast<uint32_t>(extensionLayers.size()) : 0,  // Extensions Count
        useExtensions ? extensionLayers.data() : nullptr,                   // Extensions Names
        instancePNext
    );

    vk::Result vkResult = vk::createInstance(&vkInstanceCreateInfo, nullptr, &m_VkInstance);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] failed to create instance: " << vk::to_string(vkResult) << "\n";
        return;
    }

    // The messenger needs a live instance, so it can only be created after createInstance.
    if (useValidation) CreateDebugUtilsMessengerEXT();
}

void VulkanInstance::PopulateDebugMessengerCreateInfo()
{
    m_VkDebugMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT(
        vk::DebugUtilsMessengerCreateFlagsEXT(),
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback);
}

void VulkanInstance::CreateDebugUtilsMessengerEXT()
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (!func)
    {
        std::cerr << "[Vulkan] vkCreateDebugUtilsMessengerEXT not found (is VK_EXT_debug_utils enabled?)!\n";
        return;
    }

    PopulateDebugMessengerCreateInfo();
    VkResult vkResult = func(m_VkInstance, m_VkDebugMessengerCreateInfo, nullptr, &m_VkDebugMessenger);
    if (vkResult != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] failed to create debug messenger: " << vk::to_string(static_cast<vk::Result>(vkResult)) << "\n";
        m_VkDebugMessenger = VK_NULL_HANDLE;
    }
}

void VulkanInstance::DestroyDebugUtilsMessengerExt()
{
    if (!m_VkInstance) return;

    if (auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VkInstance, "vkDestroyDebugUtilsMessengerEXT"))
    {
        func(m_VkInstance, m_VkDebugMessenger, nullptr);
    }

    m_VkDebugMessenger = VK_NULL_HANDLE;
}

GRAM_NAMESPACE_END
