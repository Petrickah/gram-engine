struct VSInput
{
    // The locations of VulkanBuffer::Vertex::GetAttributeDescription().
    [[vk::location(0)]] float2 Position : SV_POSITION;
    [[vk::location(1)]] float3 Color    : COLOR0;
};

struct VSOutput
{
	[[vk::location(0)]] float4 Position : SV_POSITION;
    [[vk::location(1)]] float4 Color : COLOR0;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Position = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.Position, 0.0, 1.0))));
    output.Color    = float4(input.Color, 1.0f);
    return output;
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    return float4(input.Color.xyz, 1.0);
}