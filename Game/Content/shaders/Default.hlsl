#include "Default.generated.hlsli"

struct VSInput
{
    [[vk::location(0)]] float2 Position : SV_POSITION;
    [[vk::location(1)]] float3 Color    : COLOR0;
    [[vk::location(2)]] float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
	[[vk::location(0)]] float4 Position : SV_POSITION;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.TexCoord = input.TexCoord;
    output.Position = mul(Params.Proj, mul(Params.View, mul(Params.Model, float4(input.Position, 0.0, 1.0))));
    output.Color    = float4(input.Color, 1.0f);
    return output;
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    return Albedo.Sample(AlbedoSampler, input.TexCoord) * input.Color;
}