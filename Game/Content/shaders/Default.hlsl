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

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Position = float4(input.Position, 0.0f, 1.0f);
    output.Color    = float4(input.Color, 1.0f);
    return output;
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    return float4(input.Color.xyz, 1.0);
}