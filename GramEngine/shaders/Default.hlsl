struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float3 Color : COLOR0;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
    [[vk::location(0)]] float3 Color : COLOR0;
};

VSOutput VSMain(in VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Color    = input.Color;
    output.Position = float4(input.Position.xyz, 1.0);
    return output;
}

float4 PSMain(in VSOutput input) : SV_TARGET
{
    return float4(input.Color, 1.0);
}