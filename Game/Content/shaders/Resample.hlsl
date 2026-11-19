#include "Resample.generated.hlsli"

// AMD FidelityFX Super Resolution 1 (ffx/, MIT, see ffx/LICENSE.txt): EASU, its edge-adaptive upscaler.
// ffx_a.h is AMD's portability layer (AF2 = float2, AU4 = uint4, ...); ffx_fsr1.h holds the algorithm and
// asks the including shader for three functions reading the input: a gather of one channel each.
#define A_GPU  1
#define A_HLSL 1
#include "ffx/ffx_a.h"

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return Color.GatherRed  (ColorSampler, p); }   // the 2x2 texels around p, red only
AF4 FsrEasuGF(AF2 p) { return Color.GatherGreen(ColorSampler, p); }
AF4 FsrEasuBF(AF2 p) { return Color.GatherBlue (ColorSampler, p); }
#include "ffx/ffx_fsr1.h"


struct VSInput
{
    [[vk::location(0)]] float2 Position : POSITION0;
    [[vk::location(1)]] float3 Color    : COLOR0;
    [[vk::location(2)]] float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    [[vk::location(0)]] float4 Position : SV_POSITION;
    [[vk::location(1)]] float3 Color    : COLOR0;
    [[vk::location(2)]] float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(in VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.Color    = input.Color;
    output.TexCoord = input.TexCoord;
    return output;
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    // The ray tracer rendered fewer pixels than the screen has (render scale under 100%): magnify.
    if (Params.ScaleFactor <= 1.0f)
    {
        if (Params.Mode == 1)
        {
            // EASU: from 12 input texels around this screen pixel, the direction and length of the edge
            // there, then a filter stretched along that edge (sharp across it, smooth along it), clamped to
            // the nearest 4 texels' range so it can't ring.
            AF3 color;
            FsrEasuF(color, AU2(input.Position.xy), asuint(Params.EasuCon0), asuint(Params.EasuCon1), asuint(Params.EasuCon2), asuint(Params.EasuCon3));
            return float4(color, 1.0f);
        }

        return float4(Color.Sample(ColorSampler, input.TexCoord).rgb, 1.0f);   // bilinear: 4 texels, blended by distance
    }

    // More pixels than the screen has (render scale over 100%): average them, SSAA.

    float3 colorSum     = float3(0.0f, 0.0f, 0.0f);
    float  totalSamples = Params.ScaleFactor * Params.ScaleFactor;
    float2 startOffset  = -0.5f * (Params.ScaleFactor - 1.0f) * Params.TexelSize;

    for (float x = 0; x < Params.ScaleFactor; x += 1.0f)
    {
        for (float y = 0; y < Params.ScaleFactor; y += 1.0f)
        {
            float2 offset = startOffset + float2(x, y) * Params.TexelSize;
            colorSum += Color.Sample(ColorSampler, input.TexCoord + offset).rgb;
        }
    }

    return float4(colorSum / totalSamples, 1.0f);
}