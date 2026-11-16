#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

struct VulkanRendererSpecification;

class VulkanShader : private MoveObject
{
public:
    using StagesMap = std::map<std::string, std::pair<std::string, vk::ShaderStageFlagBits>>;

    static VulkanShader Graphics(const std::filesystem::path &workspace, const std::string& filename, const vk::Device& device);
    static VulkanShader Compute(const std::filesystem::path &workspace, const std::string& filename, const vk::Device& device);

private:
    VulkanShader(const std::filesystem::path &workspace, const std::string& filename, const vk::Device& device, const StagesMap& stages);

public:
    bool IsValid() const
    { 
        // all_of is true on an empty map, so also check that every stage got its module.
        return !m_ShaderStages.empty() && m_ShaderStageModules.size() == m_ShaderStages.size()
            && std::all_of(m_ShaderStageModules.begin(), m_ShaderStageModules.end(), [](const auto& shaderModule) {
                return static_cast<bool>(shaderModule.second);
            });
    }

    constexpr const uint32_t size() const { return static_cast<uint32_t>(m_ShaderStageCreateInfos.size()); }
    constexpr const vk::PipelineShaderStageCreateInfo* data() const { return m_ShaderStageCreateInfos.data(); }
    
private:
    bool TryLoadShaderProgram(const vk::Device& device, const std::filesystem::path &workspace, std::string_view filename);
    bool ReadShaderFile(std::vector<char>& buffer, const std::filesystem::path& workspace, const std::string &filename);

    VkShaderModule GetShaderModule(std::string_view stageName) const
    {
        if (!m_ShaderStageModules.contains(std::string(stageName))) return VK_NULL_HANDLE;
        return static_cast<VkShaderModule>(m_ShaderStageModules.at(std::string(stageName)).get());
    }

private:
    StagesMap m_ShaderStages;

    // Each UniqueShaderModule destroys its module (it keeps its own device), so no destructor is needed,
    // and a moved-from VulkanShader holds null handles: nothing gets destroyed twice.
    std::map<std::string, vk::UniqueShaderModule> m_ShaderStageModules;
    std::vector<vk::PipelineShaderStageCreateInfo> m_ShaderStageCreateInfos;

};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
