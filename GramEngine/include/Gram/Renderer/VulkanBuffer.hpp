#pragma once

#include "Gram/Renderer/VulkanDevice.hpp"

#include "glm/glm.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanBuffer : private MoveObject
{
public:
    struct Mapping
    {
        vk::DeviceSize Size = 0;
        void*          Data = nullptr;

        static void    Unmap(vk::Device device, vk::DeviceMemory memory) { device.unmapMemory(memory); }
        static Mapping Map(vk::Device device, vk::DeviceMemory memory, vk::DeviceSize size, void* inData = nullptr)
        {
            auto memoryMapping = device.mapMemory(memory, 0, size);
            GramVulkanCheck_(memoryMapping.result, "error mapping the memory", return {});

            // Kept mapped (persistent mapping): per-frame data is written through Data. No initial
            // data (a uniform buffer filled later) means nothing to copy: memcpy from null is undefined.
            if (inData) std::memcpy(*memoryMapping, inData, static_cast<size_t>(size));
            return {
                .Size = size,
                .Data = *memoryMapping,
            };
        }
    };

    struct Vertex
    {
        glm::vec2 Position;
        glm::vec3 Color;

        static vk::VertexInputBindingDescription GetBindingDescription()
        {
            return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
        }

        static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescription()
        {
            return {{
                { .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat,    .offset = offsetof(Vertex, Position) },
                { .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, Color)    }
            }};
        }
        
        static vk::PipelineVertexInputStateCreateInfo GetVertexInputInfo()
        {
            static auto attributes = Vertex::GetAttributeDescription();
            static auto binding    = Vertex::GetBindingDescription();
            return {
                .vertexBindingDescriptionCount   = 1,
                .pVertexBindingDescriptions      = &binding,
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
                .pVertexAttributeDescriptions    = attributes.data()
            };
        }

        static VulkanBuffer Create(VulkanDevice& device, std::vector<VulkanBuffer::Vertex> vertices);
    };

public:
    VulkanBuffer() = default;
    static VulkanBuffer Create(VulkanDevice& device, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::DeviceSize size, void* data);

private:
    VulkanBuffer(VulkanDevice& device, vk::UniqueBuffer buffer, vk::UniqueDeviceMemory memory, VulkanBuffer::Mapping mapping)
        : m_Device(static_cast<vk::Device>(device.GetLogicalDevice()))
        , m_Memory(std::move(memory))
        , m_Buffer(std::move(buffer))
        , m_Mapping(mapping)
    { }

public:
    inline void BindVertex(vk::CommandBuffer cmd)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, 1, &*m_Buffer, &offset);
    }

    inline vk::Buffer     Get()     const { return *m_Buffer;       }
    inline vk::DeviceSize Size()    const { return  m_Mapping.Size; }
    inline void*          GetData() const { return  m_Mapping.Data; }

private:
    vk::Device             m_Device   = {};
    vk::UniqueDeviceMemory m_Memory   = {}; // declared before the buffer: destroyed after it
    vk::UniqueBuffer       m_Buffer   = {};
    VulkanBuffer::Mapping  m_Mapping  = {};
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
