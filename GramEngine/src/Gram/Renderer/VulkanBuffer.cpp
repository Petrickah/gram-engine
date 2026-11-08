#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanBuffer VulkanBuffer::CreateVertex(VulkanDevice& device, std::vector<VulkanBuffer::Vertex> vertices)
{
    auto staging = VulkanBuffer::Create(device, sizeof(Vertex) * vertices.size(), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, vk::BufferUsageFlagBits::eTransferSrc);
    staging.Map<VulkanBuffer::Vertex, Access::Write>().Write(vertices.data(), 0, vertices.size());
    auto vertex  = VulkanBuffer::Create(device, sizeof(Vertex) * vertices.size(), vk::MemoryPropertyFlagBits::eDeviceLocal, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer);
    auto result  = device.Submit([&](vk::CommandBuffer cmd) {
        cmd.copyBuffer(staging.Get(), vertex.Get(), vk::BufferCopy { .size = sizeof(Vertex) * vertices.size() });
        return vk::Result::eSuccess;
    });

    GramVulkanCheck_(result, "error transfering vertex data to GPU", return VulkanBuffer());
    return vertex;
}

VulkanBuffer VulkanBuffer::Create(VulkanDevice &device, vk::DeviceSize size, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags usage)
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

    return VulkanBuffer(device, size, std::move(buffer.value), std::move(memory.value), properties, usage);
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER

