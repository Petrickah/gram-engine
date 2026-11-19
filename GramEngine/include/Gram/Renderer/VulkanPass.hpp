#pragma once

#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDevice;
class VulkanShader;
class VulkanSwapchain;

class VulkanPass
{
public:
    struct Context
    {
              VulkanDevice        &Device;
        // Not const: BeginSwapchainRendering marks the frame as rendered into.
              VulkanFrame::Active &ActiveFrame;
        const VulkanSwapchain     &Swapchain;
    };

public:
    VulkanPass() = default;
    virtual ~VulkanPass() = default;
    virtual bool Record(vk::CommandBuffer cmd, VulkanPass::Context ctx) = 0;
    virtual bool OnPresented() { return true; };
    virtual bool Finish() { return true; };
    // What the profiler calls this pass (its time per frame is shown under this name).
    virtual const char* GetName() const { return "Pass"; }

protected:
    // See VulkanFrame::BeginSwapchainRendering: the first pass to call it this frame clears, the later
    // ones load. Passes don't pick: their order isn't theirs to know. End it with cmd.endRendering().
    static void BeginSwapchainRendering(const VulkanPass::Context& ctx)
    {
        ctx.ActiveFrame.Frame.BeginSwapchainRendering(ctx.ActiveFrame, ctx.Swapchain);
    }
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER