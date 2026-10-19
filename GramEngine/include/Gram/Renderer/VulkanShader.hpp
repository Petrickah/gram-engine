#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanShader : private MoveObject
{
public:
    VulkanShader(const VulkanRendererSpecification* rendererSpec, const std::string& filename, const vk::Device& device);

public:
    bool IsValid() const { return static_cast<bool>(m_ShaderModule); }

    VkShaderModule GetShaderModule() const { return static_cast<VkShaderModule>(*m_ShaderModule); }
    
private:
    bool ReadShaderFile(std::vector<char>& buffer, const std::filesystem::path& workspace, const std::string &filename);

private:
    vk::UniqueShaderModule m_ShaderModule;
};

GRAM_NAMESPACE_END