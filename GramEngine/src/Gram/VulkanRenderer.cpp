#include "Gram/VulkanRenderer.hpp"
#include "Gram/GLFWApplication.hpp"

#include <iostream>
#include <set>
#include <map>

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              messageTypes,
                                                      vk::DebugUtilsMessengerCallbackDataEXT const * pCallbackData,
                                                      void *                                         pUserData)
{
    std::cout << "[Vulkan] validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

bool Gram::QueueFamilyIndices::TryGetDeviceQueue(vk::Queue& deviceQueue, const vk::Device &device, const std::string &queueFamily)
{
    const std::map<std::string, std::optional<uint32_t>>::iterator& it = m_QueueFamiliesIndices.find(queueFamily);
    if (it != m_QueueFamiliesIndices.end())
    {
        deviceQueue = device.getQueue(it->second.value(), 0);
        return true;
    }

    return false;
}

bool Gram::QueueFamilyIndices::IsComplete()
{
    for (std::map<std::string, std::optional<uint32_t>>::iterator it = m_QueueFamiliesIndices.begin(); it != m_QueueFamiliesIndices.end(); ++it)
    {
        if (!it->second.has_value())
        {
            return false;
        }
    }

    return !m_QueueFamiliesIndices.empty();
}

bool Gram::RendererSpecification::AreExtensionsSupported(const std::vector<const char*>& extensions, bool displayDebug) const
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
            bool requested = std::any_of(extensions.begin(), extensions.end(), [&ext](const char* name) {
                return std::strcmp(ext.extensionName.data(), name) == 0;
            });

            std::cout << (requested ? "     [X] " : "     [-] ") << ext.extensionName << '\n';
        }
    }

    // Every requested extension must be offered, not just one of them.
    bool allSupported = true;
    for (const char* name : extensions)
    {
        bool found = std::any_of(supported.begin(), supported.end(), [name](const vk::ExtensionProperties& ext) {
            return std::strcmp(ext.extensionName.data(), name) == 0;
        });

        if (!found)
        {
            std::cerr << "[Vulkan] instance extension not supported: " << name << "\n";
            allSupported = false;
        }
    }

    return allSupported;
}

bool Gram::RendererSpecification::AreValidationsSupported(const std::vector<const char*>& validations, bool displayDebug) const
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
            bool requested = std::any_of(validations.begin(), validations.end(), [&layer](const char* name) {
                return std::strcmp(layer.layerName.data(), name) == 0;
            });

            std::cout << (requested ? "   - [X] " : "   - [-] ") << layer.layerName << '\n';
        }
    }

    // Every requested layer must be offered, not just one of them.
    bool allSupported = true;
    for (const char* name : validations)
    {
        bool found = std::any_of(availableLayers.begin(), availableLayers.end(), [name](const vk::LayerProperties& layer) {
            return std::strcmp(layer.layerName.data(), name) == 0;
        });

        if (!found)
        {
            std::cerr << "[Vulkan] validation layer not supported: " << name << "\n";
            allSupported = false;
        }
    }

    return allSupported;
}

Gram::VulkanRenderer::~VulkanRenderer()
{
    if (m_VkDevice) m_VkDevice.destroy();
    if (m_VkDebugMessenger) DestroyDebugUtilsMessengerExt();
    if (m_VkSurface) DestroySurfaceKHR();
    if (m_VkInstance) m_VkInstance.destroy();
}

Gram::VulkanRenderer::VulkanRenderer(const RendererSpecification& rendererSpec)
    : Renderer(rendererSpec)
{
    if (!m_RendererSpec.m_Application)
    {
        std::cerr << "[Vulkan] created without an Application!\n";
        return;
    }

    const ApplicationSpecification& appSpec = m_RendererSpec.m_Application->GetAppSpec();

    vk::ApplicationInfo vkApplicationInfo(
        appSpec.m_WindowTitle.c_str(), // Application Title
        VK_MAKE_VERSION(1, 0, 0),      // Application Version
        "Gram Engine",                 // Engine Title
        VK_MAKE_VERSION(1, 0, 0),      // Engine Version
        VK_API_VERSION_1_0,            // Vulkan API Version
        nullptr
    );

    // Only ask for layers (and the messenger) if the driver actually offers all of them;
    // otherwise createInstance fails with eErrorLayerNotPresent.
    const bool useValidation = !m_RendererSpec.m_ValidationLayers.empty()
                            && m_RendererSpec.AreValidationsSupported(m_RendererSpec.m_ValidationLayers);

    // Chained into the instance create info so the layers also report problems from createInstance/destroy.
    const void* instancePNext = nullptr;
    if (useValidation)
    {
        PopulateDebugMessengerCreateInfo();
        instancePNext = &m_VkDebugMessengerCreateInfo;
    }

    vk::InstanceCreateInfo vkInstanceCreateInfo(
        vk::InstanceCreateFlags(),
        &vkApplicationInfo, // ApplicationInfo
        useValidation ? static_cast<uint32_t>(m_RendererSpec.m_ValidationLayers.size()) : 0, // Layers Count
        useValidation ? m_RendererSpec.m_ValidationLayers.data() : nullptr,                  // Layers Names
        static_cast<uint32_t>(m_RendererSpec.m_ExtensionLayers.size()),                      // Extensions Count
        m_RendererSpec.m_ExtensionLayers.data(),                                             // Extensions Names
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

    if (!CreateSurfaceKHR())
    {
        std::cerr << "[Vulkan] couldn't create a surface!" << "\n";
        m_VkSurface = VK_NULL_HANDLE;
        return;
    }

    if (!PickPhysicalDevice())
    {
        std::cerr << "[Vulkan] couldn't select a physical device!" << "\n";
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return;
    }

    if (!CreateLogicalDevice())
    {
        std::cerr << "[Vulkan] couldn't create a logical device!" << "\n";
        m_VkDevice = VK_NULL_HANDLE;
        return;
    }
}

void Gram::VulkanRenderer::PopulateDebugMessengerCreateInfo()
{
    m_VkDebugMessengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT(
        vk::DebugUtilsMessengerCreateFlagsEXT(),
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback);
}

void Gram::VulkanRenderer::CreateDebugUtilsMessengerEXT()
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

void Gram::VulkanRenderer::DestroyDebugUtilsMessengerExt()
{
    // if (m_VkDebugMessenger == VK_NULL_HANDLE) return;

    if (auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VkInstance, "vkDestroyDebugUtilsMessengerEXT"))
    {
        func(m_VkInstance, m_VkDebugMessenger, nullptr);
    }

    m_VkDebugMessenger = VK_NULL_HANDLE;
}

bool Gram::VulkanRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    VkResult vkResult = vkEnumeratePhysicalDevices(m_VkInstance, &deviceCount, nullptr);
    if (vkResult != VkResult::VK_SUCCESS)
    {
        std::cerr << "[Vulkan] no eligible physical devices found: " << vk::to_string(static_cast<vk::Result>(vkResult)) << "\n";
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkResult = vkEnumeratePhysicalDevices(m_VkInstance, &deviceCount, devices.data());
    if (vkResult != VkResult::VK_SUCCESS)
    {
        std::cerr << "[Vulkan] couldn't find physical devices: " << vk::to_string(static_cast<vk::Result>(vkResult)) << "\n";
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return false;
    }

    if (m_RendererSpec.m_DisplayDebug)
    {
        std::cout << "[Vulkan] physical devices: " << "\n";
    }

    int32_t suitabilityScore = 0;
    std::multimap<int32_t, vk::PhysicalDevice> scoredDevices;

    for (const VkPhysicalDevice& device: devices)
    {
        const vk::PhysicalDevice& deviceInstance = vk::PhysicalDevice(device);

        if (RatePhysicalDeviceSuitability(suitabilityScore, deviceInstance))
        {
            scoredDevices.insert(std::make_pair(suitabilityScore, deviceInstance));
        }
    }

    if (scoredDevices.empty())
    {
        std::cerr << "[Vulkan] no suitable physical device!\n";
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return false;
    }
    
    m_VkPhysicalDevice = scoredDevices.lower_bound(scoredDevices.rbegin()->first)->second;
    for (const std::pair<const int32_t, vk::PhysicalDevice>& scoredDevice: scoredDevices)
    {
        if (m_RendererSpec.m_DisplayDebug)
        {
            std::cout << ((scoredDevice.second == m_VkPhysicalDevice)
                          ? "   - [X] physical device: "
                          : "   - [-] physical device: ")
                      << scoredDevice.second.getProperties().deviceName << "\n";
            
            QueueFamilyIndices indices;
            FindQueueFamilies(scoredDevice.second, indices, m_RendererSpec.m_DisplayDebug);
        }
    }
    
    FindQueueFamilies(m_VkPhysicalDevice, m_VkQueueFamilyIndices);
    return m_VkPhysicalDevice != VK_NULL_HANDLE;
}

bool Gram::VulkanRenderer::RatePhysicalDeviceSuitability(int32_t &suitabilityScore, const vk::PhysicalDevice &deviceInstance)
{
    suitabilityScore = 0;

    QueueFamilyIndices indices;
    const vk::PhysicalDeviceProperties& deviceProperties = deviceInstance.getProperties();
    const vk::PhysicalDeviceFeatures& deviceFeatures = deviceInstance.getFeatures();

    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
    {
        suitabilityScore += 100000;
    }

    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu 
        || deviceProperties.deviceType == vk::PhysicalDeviceType::eVirtualGpu)
    {
        suitabilityScore -= 50;
    }

    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eCpu)
    {
        suitabilityScore -= 500;
    }

    suitabilityScore += deviceProperties.limits.maxImageDimension2D;

    if (!FindQueueFamilies(deviceInstance, indices))
    {
        return false;
    }
    
    if (!deviceFeatures.geometryShader)
    {
        return false;
    }

    return true;
}

bool Gram::VulkanRenderer::FindQueueFamilies(const vk::PhysicalDevice &deviceInstance, QueueFamilyIndices& indices, bool showDebug)
{
    indices.m_QueuePriority = 1.0f;
    indices.m_QueueFamiliesIndices["m_PresentFamily"] = std::nullopt;
    indices.m_QueueFamiliesIndices["m_GraphicsFamily"] = std::nullopt;

    if (showDebug)
    {
        std::cout << "     [Vulkan] queue families: \n";
    }

    uint32_t index = 0;
    for (const vk::QueueFamilyProperties& queueFamily: deviceInstance.getQueueFamilyProperties())
    {
        if (!indices.m_QueueFamiliesIndices["m_GraphicsFamily"].has_value())
        {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
            {
                indices.m_QueueFamiliesIndices["m_GraphicsFamily"] = index;

                if (showDebug)
                {
                    std::cout << "        - [X] m_GraphicsFamily: " << index << "\n";
                }
            }
            else if (showDebug)
            {
                std::cout << "        - [-] m_GraphicsFamily: " << index << "\n";
            }
        }

        if (!indices.m_QueueFamiliesIndices["m_PresentFamily"].has_value())
        {
            vk::Bool32 presentSupport = false;
            vk::Result vkResult = deviceInstance.getSurfaceSupportKHR(index, m_VkSurface, &presentSupport);
    
            if (vkResult != vk::Result::eSuccess)
            {
                std::cerr << "        - [-] m_PresentFamily: " << vk::to_string(vkResult) << "\n";
                presentSupport = false;
            }
    
            if (presentSupport)
            {
                indices.m_QueueFamiliesIndices["m_PresentFamily"] = index;

                if (showDebug)
                {
                    std::cout << "        - [X] m_PresentFamily: " << index << "\n";
                }
            }
        }

        if (indices.IsComplete())
        {
            return true;
        }

        index++;
    }

    return false;
}

bool Gram::VulkanRenderer::CreateSurfaceKHR()
{
    m_VkSurface = VK_NULL_HANDLE;
    GLFWApplication* application = dynamic_cast<GLFWApplication*>(m_RendererSpec.m_Application);

    if (application == nullptr)
    {
        std::cerr << "[Vulkan] this is not a GLFWApplication!\n";
        return false;
    }

    if (glfwCreateWindowSurface(m_VkInstance, application->GetWindow(), nullptr, &m_VkSurface) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] couldn't create a surface from GLFW!\n";
        return false;
    }

    return true;
}

bool Gram::VulkanRenderer::DestroySurfaceKHR()
{
    vkDestroySurfaceKHR(m_VkInstance, m_VkSurface, nullptr);
    return true;
}

bool Gram::VulkanRenderer::CreateLogicalDevice()
{
    m_VkDevice = VK_NULL_HANDLE;
    m_VkQueueFamilyIndices.m_QueueCreateInfos.clear();

    std::set<uint32_t> uniqueFamilies;
    for (std::map<std::string, std::optional<uint32_t>>::iterator it = m_VkQueueFamilyIndices.m_QueueFamiliesIndices.begin(); it != m_VkQueueFamilyIndices.m_QueueFamiliesIndices.end(); ++it)
    {
        uniqueFamilies.insert(it->second.value());
    }

    for (uint32_t family: uniqueFamilies)
    {
        m_VkQueueFamilyIndices.m_QueueCreateInfos.push_back(vk::DeviceQueueCreateInfo(
            vk::DeviceQueueCreateFlags(), family, 1,
            &m_VkQueueFamilyIndices.m_QueuePriority,
            nullptr
        ));
    }

    vk::DeviceCreateInfo deviceCreateInfo(
        vk::DeviceCreateFlags(),
        static_cast<uint32_t>(m_VkQueueFamilyIndices.m_QueueCreateInfos.size()),
        m_VkQueueFamilyIndices.m_QueueCreateInfos.data(),
        0,       // Layers Count
        nullptr, // Layers Names
        0,       // Extensions Count
        nullptr, // Extensions Names
        &m_VkQueueFamilyIndices.m_DeviceFeatures,
        nullptr
    );

    vk::Result vkResult = m_VkPhysicalDevice.createDevice(&deviceCreateInfo, nullptr, &m_VkDevice);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] failed to create logical device: " << vk::to_string(static_cast<vk::Result>(vkResult)) << "\n";
        return false;
    }
    
    return true;
}
