#pragma once

#include "Gram/Renderer/VulkanDevice.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

template<typename K> concept BufferKind = requires
{
    typename K::Type;
    { K::Usage  } -> std::convertible_to<vk::BufferUsageFlags>;
    { K::Staged } -> std::convertible_to<bool>;
} && std::is_trivially_copyable_v<typename K::Type>;

struct IndexBuffer
{
    using Type  = uint16_t;
    static constexpr vk::IndexType        IndexType = vk::IndexType::eUint16;
    static constexpr vk::BufferUsageFlags Usage     = vk::BufferUsageFlagBits::eIndexBuffer;
    static constexpr bool                 Staged    = true;
};

struct VertexBuffer
{
    using Type = struct Vertex
    {
        glm::vec2 Position;
        glm::vec3 Color;
        glm::vec2 TexCoord;
    };

    static constexpr vk::VertexInputBindingDescription BindingDescription = 
    {
        .binding = 0, .stride = sizeof(typename VertexBuffer::Type), .inputRate = vk::VertexInputRate::eVertex
    };

    static constexpr std::array<vk::VertexInputAttributeDescription, 3> AttributeDescription = 
    {{
        { .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat,    .offset = offsetof(typename VertexBuffer::Type, Position) },
        { .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(typename VertexBuffer::Type, Color)    },
        { .location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat,    .offset = offsetof(typename VertexBuffer::Type, TexCoord) }
    }};
    
    static constexpr vk::PipelineVertexInputStateCreateInfo VertexInputInfo =
    {
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &BindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(AttributeDescription.size()),
        .pVertexAttributeDescriptions    = AttributeDescription.data()
    };

    static constexpr vk::BufferUsageFlags Usage  = vk::BufferUsageFlagBits::eVertexBuffer;
    static constexpr bool                 Staged = true;
};

template<typename T>
struct UniformBuffer
{
    using Type = T;
    static constexpr vk::BufferUsageFlags Usage  = vk::BufferUsageFlagBits::eUniformBuffer;
    static constexpr bool                 Staged = false;
};

template<typename T>
struct StorageBuffer
{
    using Type = T;
    static constexpr vk::BufferUsageFlags Usage  = vk::BufferUsageFlagBits::eStorageBuffer;
    static constexpr bool                 Staged = false;
};

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
    
public:
    template<BufferKind Kind> requires Kind::Staged
    static VulkanBuffer Create(VulkanDevice& device, const std::vector<typename Kind::Type>& data)
    {
        using T = typename Kind::Type;
        const vk::DeviceSize size = sizeof(T) * data.size();
        auto stagingBuffer = VulkanBuffer::Create(device, size, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, vk::BufferUsageFlagBits::eTransferSrc);
        stagingBuffer.Map<T, Access::Write>().Write(data.data(), 0, data.size());
        auto deviceBuffer  = VulkanBuffer::Create(device, size, vk::MemoryPropertyFlagBits::eDeviceLocal, vk::BufferUsageFlagBits::eTransferDst | Kind::Usage);
        GramVulkanCheck_(device.Submit([&](vk::CommandBuffer cmd) {
            return (cmd.copyBuffer(stagingBuffer.Get(), deviceBuffer.Get(), vk::BufferCopy { .size = size }), vk::Result::eSuccess);
        }), "error transfering data to GPU", return VulkanBuffer());
        return deviceBuffer;
    }

    template<BufferKind Kind> requires (!Kind::Staged)
    static std::array<VulkanBuffer, Gram::vkMaxFramesInFlight> Create(VulkanDevice& device, uint32_t count = 1)
    {
        std::array<VulkanBuffer, Gram::vkMaxFramesInFlight> buffers;

        for (auto& buffer: buffers)
        {
            buffer = VulkanBuffer::Create(device, count * sizeof(typename Kind::Type), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, Kind::Usage);
        }

        return buffers;
    }

    static VulkanBuffer Create(VulkanDevice& device, vk::DeviceSize size, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags usage);

    VulkanBuffer() = default;

private:
    VulkanBuffer(VulkanDevice& device, vk::DeviceSize size, vk::UniqueBuffer buffer, vk::UniqueDeviceMemory memory, vk::MemoryPropertyFlags properties = {}, vk::BufferUsageFlags usage = {})
        : m_Device(static_cast<vk::Device>(device.GetLogicalDevice()))
        , m_Memory(std::move(memory))
        , m_Buffer(std::move(buffer))
        , m_Properties(properties)
        , m_Usage(usage)
        , m_Size(size)
    {}

public:
    inline const vk::Buffer&    Get()     const { return *m_Buffer; }
    inline       vk::DeviceSize GetSize() const { return  m_Size;   }

    inline void Send(vk::CommandBuffer cmd, std::function<void(vk::CommandBuffer, VulkanBuffer& buffer)> callback)
    {
        if (callback) callback(cmd, *this);
    }

    template<typename Object, Access Mode>
    inline VulkanBuffer::Mapping<Object, Mode> Map()
    {
        // Only host-visible memory can be mapped: anything else gets an invalid mapping (Write/Read refuse it).
        const bool isHostVisible = static_cast<bool>(m_Properties & vk::MemoryPropertyFlagBits::eHostVisible);
        return (isHostVisible) ? VulkanBuffer::Mapping<Object, Mode>(m_Device, *m_Memory, 0, m_Size / sizeof(Object), sizeof(Object)) : VulkanBuffer::Mapping<Object, Mode>();
    }

private:
    vk::Device               m_Device        = {};
    vk::UniqueDeviceMemory   m_Memory        = {};
    vk::UniqueBuffer         m_Buffer        = {};
    vk::MemoryPropertyFlags  m_Properties    = {};
    vk::BufferUsageFlags     m_Usage         = {};
    vk::DeviceSize           m_Size          = {};
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
