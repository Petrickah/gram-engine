#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanBuffer VulkanBuffer::Vertex::Create(VulkanDevice& device, std::vector<VulkanBuffer::Vertex> vertices)
{
    return VulkanBuffer::Create(device, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, static_cast<vk::DeviceSize>(sizeof(Vertex) * vertices.size()), reinterpret_cast<void*>(vertices.data()));
}

VulkanBuffer VulkanBuffer::Create(VulkanDevice &device, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::DeviceSize size, void* data)
{
    vk::BufferCreateInfo bufferInfo {
        .size        = size,
        .usage       = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    vk::Device logicalDevice = static_cast<vk::Device>(device.GetLogicalDevice());

    auto buffer = logicalDevice.createBufferUnique(bufferInfo);
    GramVulkanCheck_(buffer.result, "error creating the buffer", return VulkanBuffer());

    vk::MemoryRequirements memRequirements = logicalDevice.getBufferMemoryRequirements(*buffer.value);
    auto memoryType = device.FindMemoryType(memRequirements.memoryTypeBits, properties);
    GramCheck_(memoryType.has_value(), return VulkanBuffer(), "error reading the memory type");

    vk::MemoryAllocateInfo memoryAllocateInfo {
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = *memoryType
    };

    auto memory = logicalDevice.allocateMemoryUnique(memoryAllocateInfo);
    GramVulkanCheck_(memory.result, "error allocating the buffer memory", return VulkanBuffer());
    GramVulkanCheck_(logicalDevice.bindBufferMemory(*buffer.value, *memory.value, 0), "error binding the buffer memory", return VulkanBuffer());

    VulkanBuffer::Mapping mapping = {};
    bool isMappable = static_cast<bool>((properties & vk::MemoryPropertyFlagBits::eHostVisible) == vk::MemoryPropertyFlagBits::eHostVisible);
    if (isMappable) mapping = VulkanBuffer::Mapping::Map(logicalDevice, *memory.value, size, data);

    return VulkanBuffer(device, std::move(buffer.value), std::move(memory.value), mapping);
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER

