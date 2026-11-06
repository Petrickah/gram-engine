[[vk::image_format("rgba32f")]] RWTexture2D<float4> result;

[shader("compute")]
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 256 || id.y >= 256) return;
    
    float2 uv = float2(id.xy) / 255.0f;
    result[id.xy] = float4(uv.r, uv.g, 0.0f, 1.0f);
}