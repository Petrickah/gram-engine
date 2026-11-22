#pragma once

#include <cstdint>

// What a pass binds, as the pass generator describes it (gram-common/src/pass/): Passes.generated.hpp gives
// every <Name>Params a `Stage` and a `Bindings` table of these, from the pass's .pass.ron. Plain data, no Vulkan,
// so the generated header stays light; the engine's generic pass turns it into descriptor layouts and a pipeline.
// The enumerators' names are the generator's: renaming one here means renaming it there.
namespace Gram
{

enum class PassStage : uint32_t
{
    Raster,    // VSMain + PSMain: a fullscreen quad drawn into the output
    Compute,   // CSMain: a dispatch over the output
};

enum class PassResource : uint32_t
{
    Uniform,        // ConstantBuffer<Params>, one per frame in flight
    Buffer,         // StructuredBuffer<Element>, one per frame in flight
    SampledImage,   // Texture2D + its SamplerState (a combined image sampler)
    StorageImage,   // RWTexture2D, written by a compute shader
};

// A storage image's format (None for anything else).
enum class PassFormat : uint32_t { None, Rgba32f, Rgba16f, Rgba8, R32f };

struct PassBinding
{
    uint32_t     Set;
    uint32_t     Binding;
    PassResource Resource;
    PassFormat   Format;
    const char*  Name;      // the .pass.ron's name in PascalCase ("Params" for the uniform): for messages
};

} // namespace Gram
