#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/VulkanRenderer.hpp"

GRAM_NAMESPACE_BEGIN

bool QueueFamilyIndices::TryGetDeviceIndices(std::vector<uint32_t> &indices)
{
    for (std::map<std::string, std::optional<uint32_t>>::iterator it = m_QueueFamiliesIndices.begin(); it != m_QueueFamiliesIndices.end(); ++it)
    {
        if (!it->second.has_value())
        {
            return false;
        }

        indices.push_back(it->second.value());
    }

    return !indices.empty();
}

bool QueueFamilyIndices::TryGetDeviceQueue(vk::Queue &deviceQueue, const vk::Device &device, const std::string &queueFamily)
{
    const std::map<std::string, std::optional<uint32_t>>::iterator& it = m_QueueFamiliesIndices.find(queueFamily);
    if (it != m_QueueFamiliesIndices.end())
    {
        deviceQueue = device.getQueue(it->second.value(), 0);
        return true;
    }

    return false;
}

bool QueueFamilyIndices::IsComplete()
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

bool VulkanRendererSpecification::AreDeviceExtensionsSupported(const std::vector<std::string> &extensions, bool displayDebug) const
{
    if (displayDebug)
    {
        std::cout << "     [Vulkan] device extensions: \n";
    }
    
    for (const std::string& available : extensions)
    {
        bool found = std::any_of(m_DeviceExtensions.begin(), m_DeviceExtensions.end(),
            [available](const std::string& name) { return name == available; });

        if (displayDebug && found)
        {
            std::cout << "        - [X] device extension: "
                      << available << "\n";
        }
    }

    // Every requested extension must be offered by the device, not just one of them.
    return std::all_of(m_DeviceExtensions.begin(), m_DeviceExtensions.end(),
        [&extensions](const std::string& requested) {
            return std::find(extensions.begin(), extensions.end(), requested) != extensions.end();
        });
}

VulkanDevice::VulkanDevice(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance, const vk::SurfaceKHR& surface)
    : m_VkInstance(static_cast<VkInstance>(instance))
    , m_VkSurface(static_cast<VkSurfaceKHR>(surface))
{
    if (rendererSpec && !PickPhysicalDevice(rendererSpec))
    {
        std::cerr << "[Vulkan] couldn't select a physical device!" << "\n";
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return;
    }

    if (rendererSpec && !CreateLogicalDevice(rendererSpec))
    {
        std::cerr << "[Vulkan] couldn't create a logical device!" << "\n";
        m_VkDevice = VK_NULL_HANDLE;
        return;
    }
}

VulkanDevice::~VulkanDevice()
{
    if (m_VkDevice) m_VkDevice.destroy();
}

bool VulkanDevice::PickPhysicalDevice(const VulkanRendererSpecification* rendererSpec)
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

    if (rendererSpec && rendererSpec->m_DisplayDebug)
    {
        std::cout << "[Vulkan] physical devices: " << "\n";
    }

    int32_t suitabilityScore = 0;
    std::vector<const char*> extensions;
    std::multimap<int32_t, vk::PhysicalDevice> scoredDevices;

    for (const VkPhysicalDevice& device: devices)
    {
        const vk::PhysicalDevice& deviceInstance = vk::PhysicalDevice(device);

        if (rendererSpec && RatePhysicalDeviceSuitability(rendererSpec, suitabilityScore, deviceInstance))
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
        if (rendererSpec && rendererSpec->m_DisplayDebug)
        {
            std::cout << ((scoredDevice.second == m_VkPhysicalDevice)
                          ? "   - [X] physical device: "
                          : "   - [-] physical device: ")
                      << scoredDevice.second.getProperties().deviceName << "  "
                      << scoredDevice.second.getProperties().deviceID   << "  "
                      << scoredDevice.second.getProperties().vendorID   << "\n";
            
            QueueFamilyIndices indices;
            FindQueueFamilies(rendererSpec, scoredDevice.second, indices, rendererSpec->m_DisplayDebug);

            std::vector<std::string> extensions;
            FindDeviceExtensions(rendererSpec, scoredDevice.second, extensions, rendererSpec->m_DisplayDebug);
        }
    }
    
    FindQueueFamilies(rendererSpec, m_VkPhysicalDevice, m_QueueFamilyIndices);
    FindDeviceExtensions(rendererSpec, m_VkPhysicalDevice, m_DeviceExtensions);
    return m_VkPhysicalDevice != VK_NULL_HANDLE;
}

bool VulkanDevice::RatePhysicalDeviceSuitability(const VulkanRendererSpecification* rendererSpec, int32_t &suitabilityScore, const vk::PhysicalDevice &deviceInstance)
{
    suitabilityScore = 0;

    QueueFamilyIndices indices;
    std::vector<std::string> extensions;
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

    if (rendererSpec && !FindDeviceExtensions(rendererSpec, deviceInstance, extensions))
    {
        return false;
    }

    if (rendererSpec && !FindQueueFamilies(rendererSpec, deviceInstance, indices))
    {
        return false;
    }
    
    if (!deviceFeatures.geometryShader)
    {
        return false;
    }

    return true;
}

bool VulkanDevice::FindQueueFamilies(const VulkanRendererSpecification* /*rendererSpec*/, const vk::PhysicalDevice &deviceInstance, QueueFamilyIndices& indices, bool showDebug)
{
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

bool VulkanDevice::FindDeviceExtensions(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice &deviceInstance, std::vector<std::string> &extensions, bool showDebug)
{
    uint32_t extensionCount = 0;
    vk::Result vkResult = vk::Result::eSuccess;

    vkResult = deviceInstance.enumerateDeviceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] no device extensions reported for this device: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    std::vector<vk::ExtensionProperties> availableExtensions(extensionCount);
    vkResult = deviceInstance.enumerateDeviceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] no device extensions found for this device: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    for (const vk::ExtensionProperties& currAvailableExtension: availableExtensions)
    {
        extensions.push_back(currAvailableExtension.extensionName);
    }

    if (rendererSpec && !rendererSpec->AreDeviceExtensionsSupported(extensions, showDebug))
    {
        std::cerr << "[Vulkan] not all device extensions are supported by this device!\n";
        return false;
    }

    return true;
}

bool VulkanDevice::CreateLogicalDevice(const VulkanRendererSpecification* rendererSpec)
{
    m_VkDevice = VK_NULL_HANDLE;
    m_QueueCreateInfos.clear();

    std::set<uint32_t> uniqueFamilies;
    for (std::map<std::string, std::optional<uint32_t>>::iterator it = m_QueueFamilyIndices.m_QueueFamiliesIndices.begin(); it != m_QueueFamilyIndices.m_QueueFamiliesIndices.end(); ++it)
    {
        uniqueFamilies.insert(it->second.value());
    }

    // The create infos keep a pointer to this, so it must outlive the loop.
    float queuePriority = 1.0f;
    for (uint32_t family: uniqueFamilies)
    {
        m_QueueCreateInfos.push_back(vk::DeviceQueueCreateInfo(
            vk::DeviceQueueCreateFlags(),
            family, 1, &queuePriority,
            nullptr
        ));
    }

    if (!AreExtensionsSupported(rendererSpec))
    {
        std::cerr << "[Vulkan] no device extensions are active!\n";
        return false;
    }

    // The create info only stores pointers, so these vectors must outlive createDevice.
    const std::vector<const char*> deviceExtensions = rendererSpec->GetDeviceExtensions();
    
    vk::DeviceCreateInfo deviceCreateInfo(
        vk::DeviceCreateFlags(),
        static_cast<uint32_t>(m_QueueCreateInfos.size()),
        m_QueueCreateInfos.data(),
        0, nullptr, // Validation Layers
        static_cast<uint32_t>(deviceExtensions.size()), deviceExtensions.data(), // Extension Layers
        &m_DeviceFeatures, nullptr
    );

    vk::Result vkResult = m_VkPhysicalDevice.createDevice(&deviceCreateInfo, nullptr, &m_VkDevice);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] failed to create logical device: " << vk::to_string(static_cast<vk::Result>(vkResult)) << "\n";
        return false;
    }
    
    return true;
}

GRAM_NAMESPACE_END
