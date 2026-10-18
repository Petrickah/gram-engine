#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

#include <fstream>

GRAM_NAMESPACE_BEGIN

VulkanShader::VulkanShader(const VulkanRendererSpecification* rendererSpec, const std::string& filename, const vk::Device& device)
    : m_Device(device)
{
    if (!rendererSpec || !device) return;

    if (!ReadShaderFile(rendererSpec->m_ProjectWorkspace, filename))
    {
        std::cerr << "[Vulkan] error reading the shader file.\n";
        return;
    }

    vk::Result vkResult = vk::Result::eSuccess;
    vk::ShaderModuleCreateInfo vkShaderModuleCreateInfo {
        .codeSize = m_ShaderBuffer.size() * sizeof(char),
        .pCode    = reinterpret_cast<const uint32_t*>(m_ShaderBuffer.data())
    };

    vkResult = device.createShaderModule(&vkShaderModuleCreateInfo, nullptr, &m_ShaderModule);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] error loading the shader in memory: " << vk::to_string(vkResult) << "\n";
        return;
    }
}

bool VulkanShader::ReadShaderFile(const std::filesystem::path& workspace, const std::string &filename)
{
    std::cout << "[Vulkan] reading shader file at: " << workspace / filename << "\n";
    std::ifstream file(workspace / filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "[Vulkan] couldn't open the shader file.\n";
        return false;
    }

    m_ShaderBuffer = std::vector<char>(file.tellg());
    
    file.seekg(0, std::ios::beg);
    file.read(m_ShaderBuffer.data(), static_cast<std::streamsize>(m_ShaderBuffer.size()));
    file.close();
    
    return !m_ShaderBuffer.empty();
}

VulkanShader::~VulkanShader()
{
    vk::Device device = m_Device;
    if (m_ShaderModule) device.destroyShaderModule(m_ShaderModule, nullptr);
}

GRAM_NAMESPACE_END