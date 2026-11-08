#pragma once

#include "Gram/Renderer/VulkanDevice.hpp"

#include "glm/glm.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanBuffer : public MoveObject
{
public:
    enum class Access
    {
        Write,
        Read,
    };

    template<typename Object, Access Mode>
    class Mapping : public MoveObject
    {
    public:
        Mapping() = default;
        Mapping(Mapping&& other) noexcept
            : m_Stride(other.m_Stride)
            , m_Count(other.m_Count)
            , m_Memory(other.m_Memory)
            , m_Device(other.m_Device)
            , m_Data(std::exchange(other.m_Data, nullptr))
        { }
    
        // Kept alive while using this mapping: per-frame data is manipulated through Read/Write.
        ~Mapping() { if (m_Data) m_Device.unmapMemory(m_Memory); }
        Mapping(vk::Device device, vk::DeviceMemory memory, vk::DeviceSize offset, vk::DeviceSize count, vk::DeviceSize size)
            : m_Stride(size)
            , m_Count(count)
            , m_Memory(memory)
            , m_Device(device)
        {
            auto memoryMapping = device.mapMemory(memory, offset, count * size);
            GramVulkanCheckVoid(memoryMapping.result, "error mapping the memory");
            m_Data = reinterpret_cast<Object*>(*memoryMapping);
        }

    public:
        bool IsValid() const { return m_Data != nullptr && m_Stride > 0; }

        const Object* Write(const Object* srcData, vk::DeviceSize offset, vk::DeviceSize count) requires (Mode == Access::Write)
        {
            GramCheck_(IsValid() && offset + count <= m_Count, return nullptr, "Mapping::Write: buffer not mapped or out of bounds");
            return reinterpret_cast<Object*>(std::memcpy( m_Data + offset, srcData, count * m_Stride));
        }
        const Object* Read(Object* dstData, vk::DeviceSize offset, vk::DeviceSize count) const requires (Mode == Access::Read)
        {
            GramCheck_(IsValid() && offset + count <= m_Count, return nullptr, "Mapping::Read: buffer not mapped or out of bounds");
            return reinterpret_cast<Object*>(std::memcpy(dstData,  m_Data + offset, count * m_Stride));
        }

    private:
        vk::DeviceSize   m_Stride = 0;
        vk::DeviceSize   m_Count  = 0;
        vk::DeviceMemory m_Memory = {};
        vk::Device       m_Device = {};
        Object*          m_Data   = nullptr;
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
    };
    
public:
    VulkanBuffer() = default;
    static VulkanBuffer CreateVertex(VulkanDevice& device, std::vector<VulkanBuffer::Vertex> vertices);
    static VulkanBuffer Create(VulkanDevice& device, vk::DeviceSize size, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags usage);

private:
    VulkanBuffer(VulkanDevice& device, vk::DeviceSize size, vk::UniqueBuffer buffer, vk::UniqueDeviceMemory memory, vk::MemoryPropertyFlags properties = {}, vk::BufferUsageFlags usage = {})
        : m_Device(static_cast<vk::Device>(device.GetLogicalDevice()))
        , m_Memory(std::move(memory))
        , m_Buffer(std::move(buffer))
        , m_Properties(properties)
        , m_Usage(usage)
        , m_Size(size)
    { }

public:
    inline void BindVertex(vk::CommandBuffer cmd)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, 1, &*m_Buffer, &offset);
    }

    inline vk::Buffer     Get()     const { return *m_Buffer; }
    inline vk::DeviceSize GetSize() const { return  m_Size;   }

public:
    template<typename Object, Access Mode>
    VulkanBuffer::Mapping<Object, Mode> Map()
    {
        // Only host-visible memory can be mapped: anything else gets an invalid mapping (Write/Read refuse it).
        const bool isHostVisible = static_cast<bool>(m_Properties & vk::MemoryPropertyFlagBits::eHostVisible);
        return (isHostVisible) ? VulkanBuffer::Mapping<Object, Mode>(m_Device, *m_Memory, 0, m_Size / sizeof(Object), sizeof(Object)) : VulkanBuffer::Mapping<Object, Mode>();
    }

private:
    vk::Device              m_Device     = {};
    vk::UniqueDeviceMemory  m_Memory     = {};
    vk::UniqueBuffer        m_Buffer     = {};
    vk::MemoryPropertyFlags m_Properties = {};
    vk::BufferUsageFlags    m_Usage      = {};
    vk::DeviceSize          m_Size       = {};
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
