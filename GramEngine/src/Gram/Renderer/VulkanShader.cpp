#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

#ifdef GRAM_VULKAN_RENDERER
#include <fstream>

GRAM_NAMESPACE_BEGIN

VulkanShader::VulkanShader(const VulkanRendererSpecification* rendererSpec, const std::string& filename, const vk::Device& device)
{
    if (!rendererSpec || !device) return;

    std::vector<char> buffer;
    if (ReadShaderFile(buffer, rendererSpec->m_ProjectWorkspace, filename))
    {
        vk::ShaderModuleCreateInfo vkShaderModuleCreateInfo {
            .codeSize = buffer.size() * sizeof(char),
            .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
        };
    
        auto result = device.createShaderModuleUnique(vkShaderModuleCreateInfo);
        if (result.result != vk::Result::eSuccess || !result.has_value())
        {
            GramVulkanError("error loading the shader {} in memory: {}", filename, vk::to_string(result.result));
            return;
        }

        m_ShaderModule = std::move(result.value);
    }
    else
    {
        GramVulkanError("error reading the shader file {}", filename);
    }
}

bool VulkanShader::ReadShaderFile(std::vector<char>& buffer, const std::filesystem::path& workspace, const std::string &filename)
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