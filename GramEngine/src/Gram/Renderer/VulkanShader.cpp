#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

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
            std::cerr << "[Vulkan] error loading the shader in memory: " << vk::to_string(result.result) << "\n";
            return;
        }
    
        m_ShaderModule = std::move(result.value);
    }
    else
    {
        std::cerr << "[Vulkan] error reading the shader file.\n";
    }
}

bool VulkanShader::ReadShaderFile(std::vector<char>& buffer, const std::filesystem::path& workspace, const std::string &filename)
{
    std::cout << "[Vulkan] reading shader file at: " << workspace / filename << "\n";
    std::ifstream file(workspace / filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "[Vulkan] couldn't open the shader file.\n";
        return false;
    }

    buffer.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    
    return !buffer.empty();
}

GRAM_NAMESPACE_END