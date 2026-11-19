#include "Sharpen.generated.hlsli"

// AMD FidelityFX Super Resolution 1 (ffx/, MIT, see ffx/LICENSE.txt): RCAS, its sharpening pass after EASU.
#define A_GPU  1
#define A_HLSL 1
#include "ffx/ffx_a.h"

#define FSR_RCAS_F 1
AF4  FsrRcasLoadF(ASU2 p) { return Color.Load(int3(p, 0)); }        // one texel, no filtering
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) { }       // the input is used as it is
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
    const int2 pixel = int2(input.Position.xy);   // input and output have the same size
    if (Params.Enabled == 0) return float4(Color.Load(int3(pixel, 0)).rgb, 1.0f);

    // RCAS: from this pixel and its 4 neighbours (a cross), the most sharpening that keeps the result inside
    // their min..max range (no halos), scaled by Sharpness; "robust": noise-like spots are sharpened less.
    AF3 color;
    FsrRcasF(color.r, color.g, color.b, AU2(pixel), AU4(asuint(Params.Sharpness), 0, 0, 0));
    return float4(color, 1.0f);
}
