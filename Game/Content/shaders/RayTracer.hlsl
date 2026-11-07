[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> result;

[shader("compute")]
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    result.GetDimensions(width, height);
    
    if (id.x >= width || id.y >= height) return;

    result[id.xy] = float4(float2(id.xy) / float2(width, height), 0.0f, 1.0f);
}