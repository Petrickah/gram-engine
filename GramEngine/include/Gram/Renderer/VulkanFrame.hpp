#pragma once

#include "Gram/Renderer/VulkanImage.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

struct QueueFamilyIndices;
struct VulkanRendererSpecification;

class VulkanPipeline;
class VulkanSwapchain;

class VulkanFrame: private UniqueObject
{
public:
    struct Active
    {
        vk::CommandBuffer   CommandBuffer = {};
        VulkanFrame&        Frame;
        uint32_t            FrameIndex    = 0;
        uint32_t            ImageIndex    = 0;
        // The acquired swapchain image's state (owned by the swapchain): every transition of it goes
        // through here, so End knows the layout the last pass left it in.
        VulkanImage::State& SwapchainImage;
        // Set by BeginSwapchainRendering. Still false in End means nothing drew this frame: End clears the image.
        bool                SwapchainRendered = false;
    };

public:
    VulkanFrame(const VulkanRendererSpecification* rendererSpec, const QueueFamilyIndices& indices, const vk::Device& device);
    std::optional<VulkanFrame::Active> Begin(vk::Device device, VulkanSwapchain& swapChain);
    bool End(VulkanFrame::Active& frame, VulkanDevice& device, VulkanSwapchain& swapChain);

    // Moves the acquired swapchain image to eColorAttachmentOptimal and begins dynamic rendering into it.
    // The first call of the frame clears the image (it comes from eUndefined); the later ones load, drawing on top.
    // The caller ends it with endRendering.
    void BeginSwapchainRendering(VulkanFrame::Active& frame, const VulkanSwapchain& swapChain) const;

public:
    VkCommandBuffer GetCommandBuffer    (uint32_t frameIndex) const { return static_cast<VkCommandBuffer>(*m_CommandBuffers    [frameIndex]); }
    VkFence         GetInFlightFence    (uint32_t frameIndex) const { return static_cast<VkFence>        ( m_InFlightFences    [frameIndex]); }
    VkSemaphore     GetCompleteSemaphore(uint32_t frameIndex) const { return static_cast<VkSemaphore>    ( m_CompleteSemaphores[frameIndex]); }
    VkSemaphore     GetFinishedSemaphore(uint32_t frameIndex) const { return static_cast<VkSemaphore>    ( m_FinishedSemaphores[frameIndex]); }

    void MarkSwapchainStale() { m_SwapchainStale = true; }
    bool IsSwapchainStale() const { return m_SwapchainStale; }

public:
    bool OnStart(vk::Device device, uint32_t imageCount);
    bool OnRecreateSwapchain(vk::Device device, uint32_t imageCount);
    bool OnFinish(vk::Device device);
    bool OnStop(vk::Device device);

private:
    bool CreateFinishedSemaphores(vk::Device device, uint32_t imageCount);
    bool CreateRendererObjects(vk::Device device, uint32_t imageCount);
    bool CreateCompleteSemaphores(vk::Device device);
    bool CreateFences(vk::Device device);
    void DestroyFinishedSemaphores(vk::Device device);
    void DestroyPresentCompleteSemaphores(vk::Device device);
    void DestroyFences(vk::Device device);

private:
                vk::UniqueCommandPool    m_CommandPool        = {};
    std::vector<vk::UniqueCommandBuffer> m_CommandBuffers     = {};
    std::vector<vk::Fence>               m_InFlightFences     = {};
    std::vector<vk::Semaphore>           m_CompleteSemaphores = {};
    std::vector<vk::Semaphore>           m_FinishedSemaphores = {};
                uint32_t                 m_FrameIndex         = 0;
                uint32_t                 m_ImageIndex         = 0;
                bool                     m_SwapchainStale     = false;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER