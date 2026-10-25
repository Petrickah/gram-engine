#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#include "Gram/Application/GLFWApplication.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              messageTypes,
                                                      vk::DebugUtilsMessengerCallbackDataEXT const * pCallbackData,
                                                      void *                                         pUserData)
{
    // ImGui's backend (1.92.9b docking) transitions secondary-viewport swapchain
    // images without acquiring them first; not ours to fix.
    if (pCallbackData->pMessageIdName && std::string_view(pCallbackData->pMessageIdName) == Gram::vkNonAcquiredSwapchainImageUsed)
    {
        return VK_FALSE;
    }

    // pMessageIdName is optional and may be null.
    const char* messageId = pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "-";
    switch (messageSeverity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        GramVulkanError("validation layer ({}): {}", messageId, pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        GramVulkanWarning("validation layer ({}): {}", messageId, pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        GramVulkanInfo("validation layer ({}): {}", messageId, pCallbackData->pMessage);
        break;
    default:
        GramVulkanTrace("validation layer ({}): {}", messageId, pCallbackData->pMessage);
        break;
    }

    return VK_FALSE;
}

bool VulkanRendererSpecification::AreExtensionsSupported(const std::vector<std::string> &extensions, bool displayDebug) const
{
    uint32_t extensionCount = 0;
    GramVulkanCheck(vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr), "reports no instance extensions");

    std::vector<vk::ExtensionProperties> supported(extensionCount);
    GramVulkanCheck(vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount, supported.data()), "couldn't find instance extensions");

    if (displayDebug)
    {
        GramVulkanInfo("available extensions:");
        for (const vk::ExtensionProperties& ext : supported)
        {
            bool requested = std::any_of(extensions.begin(), extensions.end(), [&ext](const std::string& name) {
                return name == ext.extensionName.data();
            });

            GramVulkanListItem(requested, "{}", ext.extensionName.data());
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
            GramVulkanError("instance extension not supported: {}", name);
            allSupported = false;
        }
    }

    return allSupported;
}

bool VulkanRendererSpecification::AreValidationsSupported(const std::vector<std::string>& validations, bool displayDebug) const
{
    uint32_t layerCount = 0;
    GramVulkanCheck(vk::enumerateInstanceLayerProperties(&layerCount, nullptr), "reports no validation layers");

    std::vector<vk::LayerProperties> availableLayers(layerCount);
    GramVulkanCheck(vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data()), "couldn't find validation layers");

    if (displayDebug)
    {
        GramVulkanInfo("available validations:");
        for (const vk::LayerProperties& layer : availableLayers)
        {
            bool requested = std::any_of(validations.begin(), validations.end(), [&layer](const std::string& name) {
                return name == layer.layerName.data();
            });

            GramVulkanListItem(requested, "{}", layer.layerName.data());
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
            GramVulkanError("validation layer not supported: {}", name);
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
    GLFWApplication* app = rendererSpec ? dynamic_cast<GLFWApplication*>(rendererSpec->m_Application.get()) : nullptr;
    if (!app)
    {
        GramVulkanError("instance created without a GLFWApplication");
        return;
    }

    vk::ApplicationInfo vkApplicationInfo {
        .pApplicationName   = app->GetAppSpec().m_WindowTitle.c_str(),
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "Gram Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = VK_API_VERSION_1_3
    };

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

    
    vk::InstanceCreateInfo vkInstanceCreateInfo {
        .pNext                   = instancePNext,
#ifdef GRAM_PLATFORM_APPLE
        .flags                   = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif // #ifdef GRAM_PLATFORM_APPLE
        .pApplicationInfo        = &vkApplicationInfo,
        .enabledLayerCount       = useValidation ? static_cast<uint32_t>(validationLayers.size()) : 0,
        .ppEnabledLayerNames     = useValidation ? validationLayers.data() : nullptr,
        .enabledExtensionCount   = useExtensions ? static_cast<uint32_t>(extensionLayers.size()) : 0,
        .ppEnabledExtensionNames = useExtensions ? extensionLayers.data() : nullptr
    };

    GramVulkanCheckVoid(vk::createInstance(&vkInstanceCreateInfo, nullptr, &m_VkInstance), "failed to create instance");

    // The messenger needs a live instance, so it can only be created after createInstance.
    if (useValidation) CreateDebugUtilsMessengerEXT();
}

void VulkanInstance::PopulateDebugMessengerCreateInfo()
{
    m_VkDebugMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debugCallback
    };
}

void VulkanInstance::CreateDebugUtilsMessengerEXT()
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (!func)
    {
        GramVulkanError("vkCreateDebugUtilsMessengerEXT not found (is VK_EXT_debug_utils enabled?)");
        return;
    }

    PopulateDebugMessengerCreateInfo();
    VkResult vkResult = func(m_VkInstance, m_VkDebugMessengerCreateInfo, nullptr, &m_VkDebugMessenger);
    if (vkResult != VK_SUCCESS)
    {
        GramVulkanError("failed to create debug messenger: {}", vk::to_string(static_cast<vk::Result>(vkResult)));
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
#endif // #ifdef GRAM_VULKAN_RENDERER