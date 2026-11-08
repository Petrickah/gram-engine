#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#ifdef GRAM_IMGUI_ENABLED
#include <imgui.h>
#endif // #ifdef GRAM_IMGUI_ENABLED

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

std::vector<uint32_t> QueueFamilyIndices::GetUniqueFamilies() const
{
    if (!IsComplete()) return {};
    if (IsShared())    return { *m_GraphicsComputeFamily };
    return { *m_GraphicsComputeFamily, *m_PresentFamily };
}

bool VulkanRendererSpecification::AreDeviceExtensionsSupported(const std::vector<std::string> &extensions, bool displayDebug) const
{
    // Items of the physical device listed right above them, hence the "extension:" prefix.
    if (displayDebug)
    {
        for (const std::string& available : extensions)
        {
            bool requested = std::any_of(m_DeviceExtensions.begin(), m_DeviceExtensions.end(),
                [&available](const std::string& name) { return name == available; });

#ifdef GRAM_PLATFORM_APPLE
            if (!requested && available == Gram::vkPortabilitySubsetExtension)
            {
                GramVulkanListAdded("extension: {}", available);
                continue;
            }
#endif // #ifdef GRAM_PLATFORM_APPLE

            GramVulkanListItem(requested, "extension: {}", available);
        }

        // Requested extensions the device doesn't offer at all wouldn't show up above.
        for (const std::string& requested : m_DeviceExtensions)
        {
            if (std::find(extensions.begin(), extensions.end(), requested) == extensions.end())
            {
                GramVulkanListMissing("extension: {}", requested);
            }
        }
    }

    // Every requested extension must be offered by the device, not just one of them.
    return std::all_of(m_DeviceExtensions.begin(), m_DeviceExtensions.end(),
        [&extensions](const std::string& requested) {
            return std::find(extensions.begin(), extensions.end(), requested) != extensions.end();
        });
}

VulkanDevice::~VulkanDevice()
{
    if (m_VkDevice) m_VkDevice.destroy();
}

vk::Result VulkanDevice::Submit(vk::Fence fence, const vk::SubmitInfo& submitInfo, const vk::PresentInfoKHR& presentInfo)
{
    if (!m_QueueFamilyIndices.IsComplete())
    {
        GramVulkanError("couldn't retrieve the device queues: queue families are incomplete");
        return vk::Result::eErrorInitializationFailed;
    }

    const vk::Queue graphicsQueue = m_VkDevice.getQueue(*m_QueueFamilyIndices.m_GraphicsComputeFamily, 0);
    const vk::Queue presentQueue  = m_VkDevice.getQueue(*m_QueueFamilyIndices.m_PresentFamily, 0);

    GramVulkanCheckResult(graphicsQueue.submit(1, &submitInfo, fence), "request failed to submit");

    // The C call on purpose: vk::Queue::presentKHR asserts on anything but success/suboptimal
    // under VULKAN_HPP_NO_EXCEPTIONS, and eErrorOutOfDateKHR is a normal outcome on resize
    // or minimize that the caller recovers from by recreating the swapchain.
    const VkPresentInfoKHR& rawPresentInfo = presentInfo;
    return static_cast<vk::Result>(vkQueuePresentKHR(static_cast<VkQueue>(presentQueue), &rawPresentInfo));
}

vk::Result VulkanDevice::Submit(std::function<vk::Result(vk::CommandBuffer)> callback)
{
    if (!callback) return vk::Result::eErrorInitializationFailed;

    if (!m_QueueFamilyIndices.IsComplete())
    {
        GramVulkanError("couldn't retrieve the device queues: queue families are incomplete");
        return vk::Result::eErrorInitializationFailed;
    }

    vk::CommandPoolCreateInfo poolInfo {
        .flags            = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = *m_QueueFamilyIndices.m_GraphicsComputeFamily,
    };

    // Create a temporary Command Pool; one time submit
    auto commandPool = m_VkDevice.createCommandPoolUnique(poolInfo);
    GramVulkanCheckResult(commandPool.result, "command pool creation failed");

    vk::CommandBufferAllocateInfo allocInfo {
        .commandPool        = *commandPool.value,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    // Create a temporary Command Buffer; one time submit
    auto commandBuffers = m_VkDevice.allocateCommandBuffersUnique(allocInfo);
    GramVulkanCheckResult(commandBuffers.result, "command buffer allocation failed");

    auto commandBuffer = *commandBuffers.value[0];

    GramVulkanCheckResult(commandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit }), "begin recording failed");
    GramVulkanCheckResult(callback(commandBuffer), "recording failed");
    GramVulkanCheckResult(commandBuffer.end(), "end recording failed");

    vk::SubmitInfo submitInfo {.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
    const vk::Queue graphicsQueue = m_VkDevice.getQueue(*m_QueueFamilyIndices.m_GraphicsComputeFamily, 0);
    GramVulkanCheckResult(graphicsQueue.submit(1, &submitInfo, nullptr), "submission failed");
    GramVulkanCheckResult(graphicsQueue.waitIdle(), "waiting for syncronization failed");
    return vk::Result::eSuccess;
}

bool VulkanDevice::AreExtensionsSupported(const VulkanRendererSpecification *rendererSpec)
{
    // Only ask for extensions if the device actually offers all of them;
    return m_QueueFamilyIndices.IsComplete() && !rendererSpec->m_DeviceExtensions.empty() && rendererSpec->AreDeviceExtensionsSupported(m_DeviceExtensions);
}

std::optional<uint32_t> VulkanDevice::FindMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const
{
    const vk::PhysicalDeviceMemoryProperties memoryProperties = m_VkPhysicalDevice.getMemoryProperties();
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; index++)
    {
        const bool allowed = typeBits & (1u << index);
        const bool matches = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
        if (allowed && matches) return index;
    }
    return std::nullopt;
}

VulkanDevice::VulkanDevice(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance, const vk::SurfaceKHR& surface)
    : m_VkInstance(instance)
    , m_VkSurface(surface)
{
    if (!rendererSpec || !m_VkInstance || !m_VkSurface) return;

    if (!PickPhysicalDevice(rendererSpec))
    {
        GramVulkanError("couldn't select a physical device");
        m_VkPhysicalDevice = VK_NULL_HANDLE;
        return;
    }

    if (!CreateLogicalDevice(rendererSpec))
    {
        GramVulkanError("couldn't create a logical device");
        m_VkDevice = VK_NULL_HANDLE;
        return;
    }
}

bool VulkanDevice::PickPhysicalDevice(const VulkanRendererSpecification* rendererSpec)
{
    uint32_t deviceCount = 0;
    m_VkPhysicalDevice = VK_NULL_HANDLE;
    GramVulkanCheck(static_cast<vk::Result>(vkEnumeratePhysicalDevices(m_VkInstance, &deviceCount, nullptr)), "no eligible physical devices found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    GramVulkanCheck(static_cast<vk::Result>(vkEnumeratePhysicalDevices(m_VkInstance, &deviceCount, devices.data())), "couldn't find physical devices");

    const bool displayDebug = rendererSpec && rendererSpec->m_DisplayDebug;

    int32_t suitabilityScore = 0;
    std::multimap<int32_t, vk::PhysicalDevice> scoredDevices;

    for (const VkPhysicalDevice& device: devices)
    {
        const vk::PhysicalDevice deviceInstance(device);

        // Listed before rating, so devices that end up rejected are visible too. Each device is
        // the header of its own list: the queue families and extensions below are its items.
        if (displayDebug)
        {
            const vk::PhysicalDeviceProperties properties = deviceInstance.getProperties();
            GramVulkanInfo("physical device: {} (id {}, vendor {})", properties.deviceName.data(), properties.deviceID, properties.vendorID);

            QueueFamilyIndices indices;
            FindQueueFamilies(rendererSpec, deviceInstance, indices, true);

            std::vector<std::string> extensions;
            FindDeviceExtensions(rendererSpec, deviceInstance, extensions, true);
        }

        if (rendererSpec && RatePhysicalDeviceSuitability(rendererSpec, suitabilityScore, deviceInstance))
        {
            scoredDevices.insert(std::make_pair(suitabilityScore, deviceInstance));
        }
    }

    if (scoredDevices.empty())
    {
        GramVulkanError("no suitable physical device");
        return false;
    }
    
    m_VkPhysicalDevice = scoredDevices.lower_bound(scoredDevices.rbegin()->first)->second;
    if (displayDebug)
    {
        GramVulkanInfo("suitable physical devices:");
        for (const std::pair<const int32_t, vk::PhysicalDevice>& scoredDevice: scoredDevices)
        {
            const vk::PhysicalDeviceProperties properties = scoredDevice.second.getProperties();
            GramVulkanListItem(scoredDevice.second == m_VkPhysicalDevice, "{} (id {}, score {})", properties.deviceName.data(), properties.deviceID, scoredDevice.first);
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

    switch (deviceProperties.deviceType)
    {
        case vk::PhysicalDeviceType::eDiscreteGpu:   suitabilityScore += 300000; break;
        case vk::PhysicalDeviceType::eIntegratedGpu: suitabilityScore += 200000; break;
        case vk::PhysicalDeviceType::eVirtualGpu:    suitabilityScore += 100000; break;
        default: break; // eCpu / eOther: only the image limit counts, always last
    }
    
    suitabilityScore += deviceProperties.limits.maxImageDimension2D; // tie-break within a tier

    const bool displayDebug = rendererSpec && rendererSpec->m_DisplayDebug;
    const char* deviceName = deviceProperties.deviceName.data();
    const uint32_t deviceID = deviceProperties.deviceID; // two drivers can expose one GPU under the same name

    if (rendererSpec && !FindDeviceExtensions(rendererSpec, deviceInstance, extensions))
    {
        if (displayDebug) GramVulkanInfo("rejected {} (id {}): missing requested device extensions", deviceName, deviceID);
        return false;
    }

    if (rendererSpec && !FindQueueFamilies(rendererSpec, deviceInstance, indices))
    {
        if (displayDebug) GramVulkanInfo("rejected {} (id {}): no graphics + present queue families", deviceName, deviceID);
        return false;
    }

    if (deviceProperties.apiVersion < VK_API_VERSION_1_3)
    {
        if (displayDebug) GramVulkanInfo("rejected {} (id {}): Vulkan 1.3 not supported", deviceName, deviceID);
        return false;
    }

#ifdef GRAM_PLATFORM_APPLE
    // Two Metal-backed drivers can expose the same GPU (MoltenVK and KosmicKrisp) with identical
    // properties; reject KosmicKrisp in favour of MoltenVK, the one offering the portability subset.
    if (std::find(extensions.begin(), extensions.end(), Gram::vkPortabilitySubsetExtension) == extensions.end())
    {
        if (displayDebug) GramVulkanInfo("rejected {} (id {}): no portability subset extension", deviceName, deviceID);
        return false;
    }
#endif
    
#ifdef GRAM_GEOMETRY_SHADERS
    if (!deviceFeatures.geometryShader)
    {
        if (displayDebug) GramVulkanInfo("rejected {} (id {}): no geometryShader feature", deviceName, deviceID);
        return false;
    }
#endif // GRAM_GEOMETRY_SHADERS

    return true;
}

bool VulkanDevice::FindQueueFamilies(const VulkanRendererSpecification* /*rendererSpec*/, const vk::PhysicalDevice &deviceInstance, QueueFamilyIndices& indices, bool showDebug)
{
    indices = {};

    // Both flags, not either: the comparison is on the masked flags, not on their truth value.
    const vk::QueueFlags graphicsCompute = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

    uint32_t index = 0;
    for (const vk::QueueFamilyProperties& queueFamily: deviceInstance.getQueueFamilyProperties())
    {
        const bool hasGraphicsCompute = (queueFamily.queueFlags & graphicsCompute) == graphicsCompute;

        vk::Bool32 presentSupport = false;
        vk::Result vkResult = deviceInstance.getSurfaceSupportKHR(index, m_VkSurface, &presentSupport);
        if (vkResult != vk::Result::eSuccess)
        {
            GramVulkanWarning("queue family {}: present support query failed: {}", index, vk::to_string(vkResult));
            presentSupport = false;
        }

        if (showDebug)
        {
            GramVulkanListItem(hasGraphicsCompute, "queue family {}: graphics + compute", index);
            GramVulkanListItem(presentSupport,     "queue family {}: present", index);
        }

        // Best case, and the usual one: a single family for everything, so the swapchain
        // stays exclusive and no work crosses families. It wins over earlier partial picks.
        if (hasGraphicsCompute && presentSupport)
        {
            indices.m_GraphicsComputeFamily = index;
            indices.m_PresentFamily         = index;
            return true;
        }

        if (hasGraphicsCompute && !indices.m_GraphicsComputeFamily) indices.m_GraphicsComputeFamily = index;
        if (presentSupport     && !indices.m_PresentFamily)         indices.m_PresentFamily         = index;

        index++;
    }

    // Fallback: graphics + compute and present in two different families.
    return indices.IsComplete();
}

bool VulkanDevice::FindDeviceExtensions(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice &deviceInstance, std::vector<std::string> &extensions, bool showDebug)
{
    uint32_t extensionCount = 0;
    GramVulkanCheck(deviceInstance.enumerateDeviceExtensionProperties(nullptr, &extensionCount, nullptr), "no device extensions reported for this device");
    
    std::vector<vk::ExtensionProperties> availableExtensions(extensionCount);
    GramVulkanCheck(deviceInstance.enumerateDeviceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()), "no device extensions found for this device");

    for (const vk::ExtensionProperties& currAvailableExtension: availableExtensions)
    {
        extensions.push_back(currAvailableExtension.extensionName);
    }

    if (rendererSpec && !rendererSpec->AreDeviceExtensionsSupported(extensions, showDebug))
    {
        const vk::PhysicalDeviceProperties properties = deviceInstance.getProperties();
        GramVulkanError("not all device extensions are supported by {} (id {})", properties.deviceName.data(), properties.deviceID);
        return false;
    }

    return true;
}

bool VulkanDevice::CreateLogicalDevice(const VulkanRendererSpecification* rendererSpec)
{
    m_VkDevice = VK_NULL_HANDLE;
    m_QueueCreateInfos.clear();

    const std::vector<uint32_t> uniqueFamilies = m_QueueFamilyIndices.GetUniqueFamilies();
    GramCheck(!uniqueFamilies.empty(), "queue families are incomplete");

    // The create infos keep a pointer to this, so it must outlive the loop.
    float queuePriority = 1.0f;
    for (uint32_t family: uniqueFamilies)
    {
        m_QueueCreateInfos.push_back(vk::DeviceQueueCreateInfo {
            .queueFamilyIndex = family,
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority
        });
    }

    // The create info only stores pointers, so these vectors must outlive createDevice.
    std::vector<const char*> deviceExtensions = rendererSpec->GetDeviceExtensions();

#ifdef GRAM_PLATFORM_APPLE
    // Spec: a device that offers VK_KHR_portability_subset (MoltenVK) must have it enabled.
    // Conformant drivers don't offer it, so it can't be a hard requirement in main.
    if (std::find(m_DeviceExtensions.begin(), m_DeviceExtensions.end(), Gram::vkPortabilitySubsetExtension) != m_DeviceExtensions.end())
    {
        deviceExtensions.push_back(Gram::vkPortabilitySubsetExtension);
    }
#endif // #ifdef GRAM_PLATFORM_APPLE

    if (!AreExtensionsSupported(rendererSpec))
    {
        GramVulkanError("no device extensions are active");
        return false;
    }

#ifdef GRAM_GEOMETRY_SHADERS
    m_DeviceFeatures.geometryShader = VK_TRUE;
#endif

    vk::PhysicalDeviceVulkan13Features features13 {
        .synchronization2 = vk::True,
        .dynamicRendering = vk::True
    };

    vk::PhysicalDeviceFeatures2 features2 {
        .pNext    = &features13,
        .features = m_DeviceFeatures   // geometryShader & co. move in here
    };

    // pEnabledFeatures stays null: the spec forbids it alongside a VkPhysicalDeviceFeatures2 in pNext.
    vk::DeviceCreateInfo deviceCreateInfo {
        .pNext                   = &features2,
        .queueCreateInfoCount    = static_cast<uint32_t>(m_QueueCreateInfos.size()),
        .pQueueCreateInfos       = m_QueueCreateInfos.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data()
    };

    GramVulkanCheck(m_VkPhysicalDevice.createDevice(&deviceCreateInfo, nullptr, &m_VkDevice), "failed to create logical device");
    
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER