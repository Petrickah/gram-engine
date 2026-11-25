#pragma once

#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/PassLayout.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#include <optional>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

template <typename Params>
class VulkanShaderPass : public VulkanPass
{
private:
    template <typename T> static inline constexpr char ElementTag = 0;

    struct ImageSlot
    {
        std::shared_ptr<VulkanRenderTarget> Target;                     // a target that may be recreated (resized)
        const VulkanImage*                  Fixed      = nullptr;       // or an image that never changes (the skybox)
        std::optional<uint64_t>             Generation = std::nullopt;  // what the descriptor points at now; empty = never written
    };

    struct BufferSlot
    {
        std::array<VulkanBuffer, vkMaxFramesInFlight> PerFrame;                // one per frame in flight, like the uniform
        uint32_t                                      Capacity    = 0;         // elements
        const void*                                   ElementType = nullptr;   // the element type it was created for
    };

public:
    // outputFormat: what a Raster pass draws into; a Compute pass ignores it.
    VulkanShaderPass(VulkanDevice& device, const VulkanShader& shader, vk::Format outputFormat = vk::Format::eUndefined)
        : m_Descriptors(VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, LayoutBindings(0)))
        , m_ImageDescriptors(VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, LayoutBindings(1)))
        , m_Pipeline(CreatePipeline(device, shader, outputFormat, std::array { m_Descriptors.GetLayout(), m_ImageDescriptors.GetLayout() }))
    {
        if constexpr (HasUniform)
        {
            m_Uniforms = VulkanBuffer::Create<UniformBuffer<Params>>(device);
            m_Descriptors.Write(device, Params::UniformBinding, m_Uniforms, sizeof(Params));
        }
    }

    const char* GetName() const override { return Params::Shader; }
    bool IsValid() const { return m_Pipeline.IsValid(); }

    void SetImage(uint32_t binding, std::shared_ptr<VulkanRenderTarget> target)
    {
        m_Images[binding].Target = target;
    }

    void SetImage(uint32_t binding, const VulkanImage& image)
    {
        m_Images[binding].Fixed = &image;
    }

protected:
    static constexpr uint32_t ImageCount = Params::InputCount + Params::OutputCount;
    static constexpr uint32_t Set0Count  = static_cast<uint32_t>(std::ranges::count_if(Params::Bindings, [](const PassBinding& b) { return b.Set == 0; }));
    
    static constexpr bool HasUniform = std::ranges::any_of(    // A
        Params::Bindings.cbegin(), Params::Bindings.cend(), [](const Gram::PassBinding& binding)
        {
            return binding.Resource == PassResource::Uniform;
        }
    );
    static constexpr PassResource ImageResource(uint32_t binding)
    {
        auto found = std::find_if(Params::Bindings.begin(), Params::Bindings.end(), [binding](const Gram::PassBinding& passBinding)
        {
            return passBinding.Set == 1 && passBinding.Binding == binding;
        });

        if (found != Params::Bindings.end()) return found->Resource;
        return {};
    }
    static constexpr vk::DescriptorType ToDescriptorType(PassResource resource) // B
    {
        vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;

        switch (resource)
        {
        case PassResource::Uniform:
            type = vk::DescriptorType::eUniformBuffer;
            break;
        case PassResource::Buffer:
            type = vk::DescriptorType::eStorageBuffer;
            break;
        case PassResource::SampledImage:
            type = vk::DescriptorType::eCombinedImageSampler;
            break;
        case PassResource::StorageImage:
            type = vk::DescriptorType::eStorageImage;
            break;
        }

        return type;
    }
    static std::vector<vk::DescriptorSetLayoutBinding> LayoutBindings(uint32_t set) // C
    {
        std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
        for (const Gram::PassBinding& binding: Params::Bindings)
        {
            if (binding.Set != set) continue;

            layoutBindings.push_back(vk::DescriptorSetLayoutBinding {
                .binding = binding.Binding,
                .descriptorType = ToDescriptorType(binding.Resource),
                .descriptorCount = 1,
                .stageFlags = Params::Stage == Gram::PassStage::Compute
                            ? vk::ShaderStageFlagBits::eCompute
                            : vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            });
        }

        return layoutBindings;
    }
    static VulkanPipeline CreatePipeline(VulkanDevice& device, const VulkanShader& shader, vk::Format outputFormat, std::span<const vk::DescriptorSetLayout> layouts)
    {
        if constexpr (Params::Stage == PassStage::Compute)
        {
            return VulkanPipeline::Compute(device, shader, layouts);
        }
        else if constexpr (Params::Stage == PassStage::Raster)
        {
            return VulkanPipeline::Graphics(device, shader, layouts, outputFormat, VertexBuffer::VertexInputInfo);
        }
    }

    template <typename Element>
    void CreateBuffer(VulkanDevice& device, uint32_t binding, uint32_t capacity)
    {
        BufferSlot& slot = m_Buffers[binding];
        slot.PerFrame    = VulkanBuffer::Create<StorageBuffer<Element>>(device, capacity);
        slot.Capacity    = capacity;
        slot.ElementType = &ElementTag<Element>;

        m_Descriptors.Write(device, binding, slot.PerFrame, capacity * sizeof(Element));
    }

    template <typename Element>
    bool Upload(VulkanPass::Context ctx, uint32_t binding, std::span<const Element> data)
    {
        BufferSlot& slot = m_Buffers[binding];
        GramCheck(slot.ElementType == &ElementTag<Element>, "{}: binding {} holds another element type", Params::Shader, binding);
        GramCheck(data.size() <= slot.Capacity, "{}: binding {} holds a different number of elements", Params::Shader, binding);

        if (data.empty()) return true;
        slot.PerFrame[ctx.ActiveFrame.FrameIndex].template Map<Element, VulkanBuffer::Access::Write>().Write(data.data(), 0, data.size());
        return true;
    }

public:
    bool Record(vk::CommandBuffer cmd, VulkanPass::Context ctx) override
    {
        // 1. The derived pass first: resizing an output recreates its image (a new generation), seen below.
        if (!OnPrepare(ctx)) return false;

        // 2. Set 1: point each binding at its image (again when the image was recreated), then bring the image
        //    into the layout the shader uses it in.
        constexpr auto readStage = Params::Stage == PassStage::Compute ? vk::PipelineStageFlagBits2::eComputeShader
                                                                       : vk::PipelineStageFlagBits2::eFragmentShader;
        for (uint32_t i = 0; i < m_Images.size(); i++)
        {
            ImageSlot& slot = m_Images[i];

            if (slot.Fixed)   // never recreated, already readable: written once, no transition
            {
                if (!slot.Generation)
                {
                    m_ImageDescriptors.Write(ctx.Device, i, *slot.Fixed);
                    slot.Generation = 0;
                }

                continue;
            }

            GramCheck(slot.Target && slot.Target->GetImage().IsValid(), "{}: image {} was not set", Params::Shader, i);
            VulkanImage& image = slot.Target->GetImage();

            // Written by the shader (RWTexture2D) or read by it (Texture2D): everything else follows from that.
            const bool storage = ImageResource(i) == PassResource::StorageImage;
            const auto layout  = storage ? vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal;

            if (slot.Generation != slot.Target->GetGeneration())
            {
                // Safe now: a target recreates its image only after a waitIdle, so no frame still uses these sets.
                m_ImageDescriptors.Write(ctx.Device, i, image, layout);
                slot.Generation = slot.Target->GetGeneration();
            }

            if (storage) image.Transition(cmd, layout, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);
            else         image.Transition(cmd, layout, readStage,                                  vk::AccessFlagBits2::eShaderSampledRead);
        }

        // 3. Set 0's uniform: the derived pass fills it, the base copies it into this frame's buffer.
        if constexpr (HasUniform)
        {
            Params params {};
            Fill(params, ctx);
            m_Uniforms[ctx.ActiveFrame.FrameIndex].template Map<Params, VulkanBuffer::Access::Write>().Write(&params, 0, 1);
        }

        // 4. The pipeline and both sets, for this frame.
        const uint32_t frame = ctx.ActiveFrame.FrameIndex;
        m_Pipeline.Bind(cmd);
        m_Pipeline.BindDescriptorSets(cmd, 0, std::array { m_Descriptors.Get(frame), m_ImageDescriptors.Get(frame) });

        // 5. What only this pass knows: the dispatch or the draw.
        return OnRecord(cmd, ctx);
    }

protected:
    virtual bool OnPrepare(VulkanPass::Context ctx) { return true; }
    virtual void Fill(Params& params, VulkanPass::Context ctx) { }
    virtual bool OnRecord(vk::CommandBuffer cmd, VulkanPass::Context ctx) = 0;

    std::array<VulkanBuffer, vkMaxFramesInFlight> m_Uniforms;
    std::array<ImageSlot, ImageCount>             m_Images;
    std::array<BufferSlot, Set0Count>             m_Buffers;
    VulkanDescriptors                             m_Descriptors;        // set 0
    VulkanDescriptors                             m_ImageDescriptors;   // set 1
    VulkanPipeline                                m_Pipeline;           // D
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER