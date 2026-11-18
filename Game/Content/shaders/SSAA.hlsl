#include "SSAA.generated.hlsli"


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