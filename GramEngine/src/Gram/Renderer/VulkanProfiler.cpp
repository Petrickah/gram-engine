#include "Gram/Renderer/VulkanProfiler.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

bool VulkanProfiler::Create(VulkanDevice& device)
{
    if (device.GetTimestampValidBits() == 0) return false;
    m_Device = static_cast<vk::Device>(device.GetLogicalDevice());
    m_Period = device.GetTimestampPeriod();

    for (vk::UniqueQueryPool& pool : m_Pools)
    {
        auto created = m_Device.createQueryPoolUnique(vk::QueryPoolCreateInfo {
            .queryType  = vk::QueryType::eTimestamp,
            .queryCount = 2 * MaxScopes,             // a start and an end per scope
        });

        GramVulkanCheck(created.result, "VulkanProfiler: couldn't create a query pool");
        pool = std::move(created.value);
    }

    m_Valid = true;
    return true;
}

void VulkanProfiler::BeginFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    if (!m_Valid) return;
    m_Frame = frameIndex;
    m_Query = 0;

    // 1. This slot's last frame is done (VulkanFrame::Begin waited for its fence): read its times.
    std::vector<std::string>& names = m_Names[frameIndex];

    if (!names.empty())
    {
        std::vector<uint64_t> stamps(2 * names.size());
        const vk::Result read = m_Device.getQueryPoolResults(
            *m_Pools[frameIndex],
            0,
            static_cast<uint32_t>(stamps.size()),
            stamps.size() * sizeof(uint64_t),
            stamps.data(), sizeof(uint64_t),
            vk::QueryResultFlagBits::e64
        );

        if (read == vk::Result::eSuccess)
        {
            for (size_t i = 0; i < names.size(); i++)
            {
                const float ms = static_cast<float>(stamps[2 * i + 1] - stamps[2 * i]) * m_Period / 1'000'000.0f;   // ticks → ns → ms
                Smooth(names[i], ms);
            }
        }
    }

    names.clear();

    // 2. Empty its queries again before this frame writes into them (a query can't be written twice unreset).
    cmd.resetQueryPool(*m_Pools[frameIndex], 0, 2 * MaxScopes);
}

void VulkanProfiler::Begin(vk::CommandBuffer cmd, const char* name)
{
    if (!m_Valid || m_Names[m_Frame].size() >= MaxScopes) return;   // full: this scope isn't measured
    m_Names[m_Frame].push_back(name);
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *m_Pools[m_Frame], m_Query++);
}

void VulkanProfiler::End(vk::CommandBuffer cmd)
{
    // An odd count means a Begin is open; even means it was skipped (full), so there's nothing to end.
    if (!m_Valid || m_Query % 2 == 0) return;
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *m_Pools[m_Frame], m_Query++);
}

void VulkanProfiler::Smooth(const std::string& name, float ms)
{
    for (Timing& timing : m_Timings)
    {
        if (timing.Name == name) { timing.Ms = 0.9f * timing.Ms + 0.1f * ms; return; }   // mostly the past, a bit of the new
    }
    
    m_Timings.push_back({ name, ms });   // first time seen: keeps the order passes run in
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER