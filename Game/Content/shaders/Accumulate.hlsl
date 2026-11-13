[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] Texture2D    _MainTexture;
[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] SamplerState sampler_MainTexture;
[[vk::binding(1, 1)]] [[vk::combinedImageSampler]] Texture2D    _DepthTexture;
[[vk::binding(1, 1)]] [[vk::combinedImageSampler]] SamplerState sampler_DepthTexture;

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

struct UniformBuffer
{
    uint FrameCount; float3 Padding0;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

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
    float3 colorSample = _MainTexture.Sample(sampler_MainTexture, input.TexCoord).rgb;
    return float4(colorSample, 1.0f / float(ubo.FrameCount + 1));
}