#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanShader
{
public:

    VulkanShader(const VulkanShader&) = delete;
    VulkanShader(VulkanShader&& other) noexcept
        : m_Device      (std::exchange(other.m_Device, nullptr))
        , m_ShaderModule(std::exchange(other.m_ShaderModule, nullptr))
        , m_ShaderBuffer(std::move(other.m_ShaderBuffer))
    { }

    VulkanShader& operator=(const VulkanShader&) = delete;
    VulkanShader& operator=(VulkanShader&& other) noexcept
    {
        if (this != &other)
        {
            if (m_ShaderModule) m_Device.destroyShaderModule(m_ShaderModule); // free what we own first
            m_Device       = std::exchange(other.m_Device, nullptr);
            m_ShaderModule = std::exchange(other.m_ShaderModule, nullptr);
            m_ShaderBuffer = std::move(other.m_ShaderBuffer);
        }
        return *this;
    }

    VulkanShader(const VulkanRendererSpecification* rendererSpec, const std::string& filename, const vk::Device& device);
    ~VulkanShader();

public:
    bool IsValid() const { return m_ShaderModule && !m_ShaderBuffer.empty(); }

    const vk::ShaderModule& GetShaderModule() const { return m_ShaderModule; }
    
private:
    bool ReadShaderFile(const std::filesystem::path& workspace, const std::string &filename);

private:
    vk::Device m_Device;
    vk::ShaderModule m_ShaderModule;

    std::vector<char> m_ShaderBuffer;
};

GRAM_NAMESPACE_END