struct VSOutput
{
	float4 Position : SV_POSITION;
    [[vk::location(0)]] float4 Color : COLOR0;
};

[shader("vertex")]
VSOutput VSMain(uint idx : SV_VertexID)
{
    VSOutput output = (VSOutput)0;

    // Define positions directly inside an array or switch statement
    float2 positions[3] = {
        float2( 0.0f, -0.5f), // Vertex 0 (Top)
        float2( 0.5f,  0.5f), // Vertex 1 (Bottom-Right)
        float2(-0.5f,  0.5f)  // Vertex 2 (Bottom-Left)
    };

    float3 colors[3] = {
        float3(1.0f, 0.0f, 0.0f), // Red
        float3(0.0f, 1.0f, 0.0f), // Green
        float3(0.0f, 0.0f, 1.0f)  // Blue
    };

    output.Position = float4(positions[idx], 0.0f, 1.0f);
    output.Color    = float4(colors[idx], 1.0f);
    return output;
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    return float4(input.Color.xyz, 1.0);
}