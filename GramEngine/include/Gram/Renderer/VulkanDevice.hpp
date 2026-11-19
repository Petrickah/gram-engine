#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanFrame;
struct VulkanRendererSpecification;

// One family does graphics and compute (the spec guarantees one exists on any device with
// graphics), so compute work shares the graphics queue; present may be a separate family.
struct QueueFamilyIndices
{
    std::optional<uint32_t> m_GraphicsComputeFamily;
    std::optional<uint32_t> m_PresentFamily;

    bool IsComplete() const { return m_GraphicsComputeFamily.has_value() && m_PresentFamily.has_value(); }
    bool IsShared() const   { return IsComplete() && *m_GraphicsComputeFamily == *m_PresentFamily; }

    // The distinct families, for the device's queue create infos and a concurrent swapchain; empty if incomplete.
    std::vector<uint32_t> GetUniqueFamilies() const;
};

class VulkanDevice : private UniqueObject
{
public:
    VulkanDevice(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance, const vk::SurfaceKHR& surface);
    ~VulkanDevice();

public:
    // Returns the present result as-is: eSuboptimalKHR / eErrorOutOfDateKHR mean "recreate the swapchain", not "failed".
    vk::Result Submit(vk::Fence fence, const vk::SubmitInfo& submitInfo, const vk::PresentInfoKHR& presentInfo);
    vk::Result Submit(std::function<vk::Result(vk::CommandBuffer)> callback);
    bool AreExtensionsSupported(const VulkanRendererSpecification* rendererSpec);

    std::optional<uint32_t> FindMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const;

    QueueFamilyIndices& GetQueueFamilyIndices() { return m_QueueFamilyIndices; }
    const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

    float    GetTimestampPeriod()    const { return m_TimestampPeriod; }      // nanoseconds per tick: limits.timestampPeriod
    uint32_t GetTimestampValidBits() const { return m_TimestampValidBits; }   // the graphics queue family's; 0 = no timestamps

    VkDevice GetLogicalDevice() const  { return static_cast<VkDevice>(m_VkDevice); }
    VkPhysicalDevice GetPhysicalDevice() const { return static_cast<VkPhysicalDevice>(m_VkPhysicalDevice); }

private:
    bool PickPhysicalDevice(const VulkanRendererSpecification* rendererSpec);
    bool RatePhysicalDeviceSuitability(const VulkanRendererSpecification* rendererSpec, int32_t& suitabilityScore, const vk::PhysicalDevice& deviceInstance);
    bool FindQueueFamilies(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, QueueFamilyIndices& indices, bool showDebug = false);
    bool FindDeviceExtensions(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, std::vector<std::string>& extensions, bool showDebug = false);
    bool CreateLogicalDevice(const VulkanRendererSpecification* rendererSpec);

private:
    vk::Instance m_VkInstance;
    vk::SurfaceKHR m_VkSurface;

    std::vector<std::string> m_DeviceExtensions;
    std::vector<vk::DeviceQueueCreateInfo> m_QueueCreateInfos;
    
    vk::Device m_VkDevice;
    vk::PhysicalDevice m_VkPhysicalDevice;
    vk::PhysicalDeviceFeatures m_DeviceFeatures;
    QueueFamilyIndices m_QueueFamilyIndices;

    float    m_TimestampPeriod    = 0.0f;
    uint32_t m_TimestampValidBits = 0;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER