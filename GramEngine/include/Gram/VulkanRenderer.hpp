#pragma once

#include "vulkan/vulkan.hpp"

#include "../Renderer.hpp"
#include "../Application.hpp"

namespace Gram {
    class VulkanRenderer: public Renderer {
    public:
        VulkanRenderer() = default;
        virtual ~VulkanRenderer() override;
        virtual void Init(uint32_t extensionsCount = 0, const char** extensions = nullptr) override;
    private:
        vk::Instance m_VkInstance;
    };
}