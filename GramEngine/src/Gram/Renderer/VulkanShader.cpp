#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

#include <fstream>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanShader::VulkanShader(const std::filesystem::path &workspace, const std::string& filename, const vk::Device& device, const StagesMap& stages)
    : m_ShaderStages(stages)
{
    if (!device) return;

    GramCheckVoid(TryLoadShaderProgram(device, workspace, filename), "VulkanShader::VulkanShader: shader program loading failed");
}

VulkanShader VulkanShader::Graphics(const std::filesystem::path &workspace, const std::string &filename, const vk::Device &device)
{
    return VulkanShader(workspace, filename, device,
    {
        { "VSMain", { "vert.spv", vk::ShaderStageFlagBits::eVertex   } },
        { "PSMain", { "frag.spv", vk::ShaderStageFlagBits::eFragment } },
    });
}

VulkanShader VulkanShader::Compute(const std::filesystem::path &workspace, const std::string &filename, const vk::Device &device)
{
    return VulkanShader(workspace, filename, device,
    {
        { "CSMain", { "compute.spv", vk::ShaderStageFlagBits::eCompute } }
    });
}

bool VulkanShader::TryLoadShaderProgram(const vk::Device& device, const std::filesystem::path &workspace, std::string_view filename)
{
    // const auto&, not const std::pair<std::string, …>&: the map's elements are pair<const std::string, …>,
    // so naming the type without the const key copies each one into a temporary, and pName would point into it.
    for (const auto& shaderStage : m_ShaderStages)
    {
        std::vector<char> buffer;
        if (ReadShaderFile(buffer, workspace, std::format("shaders/{}.{}", filename, shaderStage.second.first)))
        {
            vk::ShaderModuleCreateInfo vkShaderModuleCreateInfo {
                .codeSize = buffer.size() * sizeof(char),
                .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
            };
        
            auto result = device.createShaderModuleUnique(vkShaderModuleCreateInfo);
            if (result.result != vk::Result::eSuccess || !result.has_value())
            {
                GramVulkanError("{}: error loading the shader {} in memory: {}", shaderStage.first, filename, vk::to_string(result.result));
                return false;
            }

            // Insert first: GetShaderModule reads the map, so it must already hold this stage.
            m_ShaderStageModules.emplace(shaderStage.first, std::move(result.value));

            m_ShaderStageCreateInfos.emplace_back(vk::PipelineShaderStageCreateInfo{
                .stage = shaderStage.second.second,
                .module = GetShaderModule(shaderStage.first),
                .pName = shaderStage.first.data()
            });
        }
        else
        {
            GramVulkanError("{}: error reading the shader file {}", shaderStage.first, filename);
            return false;
        }
    }
    
    return true;
}

bool VulkanShader::ReadShaderFile(std::vector<char> &buffer, const std::filesystem::path &workspace, const std::string &filename)
{
    const std::filesystem::path path = workspace / filename;
    GramVulkanInfo("reading shader file at: {}", path.string());
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        GramVulkanError("couldn't open the shader file {}", path.string());
        return false;
    }

    buffer.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    
    return !buffer.empty();
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER