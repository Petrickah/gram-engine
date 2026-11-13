// Depth of field as a post-process: the ray tracer renders a pinhole image (everything sharp) plus its depth,
// and this pass blurs each pixel by its circle of confusion (CoC). Wired by main.cpp, after Accumulate and
// before SSAA; the uniform is filled from the RayTracer panel's lens (DepthOfFieldParams).
// The depth of field algorithm:
//  1. This pixel's CoC: CoCScale * abs(depth - FocusDistance) / depth, capped at MaxCoC.
//  2. Gather: average the colors of the neighbours inside a disc of that diameter (TexelSize turns
//     pixels into UV offsets). A fixed set of offsets on a disc (rings, or a spiral) keeps it cheap.
//  3. Then the hard part: a sharp subject must not take color from a blurred background behind it.
//     Weigh each neighbour by whether its own CoC reaches this pixel.

[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] Texture2D        _MainTexture;    // the accumulated color
[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] SamplerState     sampler_MainTexture;
[[vk::binding(1, 1)]] [[vk::combinedImageSampler]] Texture2D<float> _DepthTexture;   // metres along the camera's axis, 1e6 = sky
[[vk::binding(1, 1)]] [[vk::combinedImageSampler]] SamplerState     sampler_DepthTexture;

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

// Layout kept 1:1 with DepthOfFieldParams (main.cpp): 32 bytes.
struct UniformBuffer
{
    float2 TexelSize; float FocusDistance; float CoCScale;   // CoC diameter in pixels = CoCScale * |d - FocusDistance| / d
    float  MaxCoC   ; uint  Enabled      ; float2 Padding0;  // Enabled 0 = the lens blurs in the ray tracer: pass through
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

static const uint  SampleCount = 48;
static const float GoldenAngle = 2.39996323f;   // π · (3 − √5) radians

[shader("vertex")]
VSOutput VSMain(in VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.Color    = input.Color;
    output.TexCoord = input.TexCoord;
    return output;
}

// Depth is data, not color: read the texel itself (Load), never a filtered mix of two depths.
// The output has the input's size, so SV_POSITION.xy is this pixel's center in the input too.
float LoadDepth(int2 pixel)
{
    return _DepthTexture.Load(int3(pixel, 0));
}

float CircleOfConfusion(float depth)
{
    return min(ubo.CoCScale * abs(depth - ubo.FocusDistance) / depth, ubo.MaxCoC);
}

[shader("pixel")]
float4 PSMain(in VSOutput input) : SV_TARGET
{
    const float3 color = _MainTexture.SampleLevel(sampler_MainTexture, input.TexCoord, 0).rgb;
    if (ubo.Enabled == 0) return float4(color, 1.0f);

    uint width, height;
    _DepthTexture.GetDimensions(width, height);

    const int2  pixel    = int2(input.Position.xy);
    const float pixelCoC = CircleOfConfusion(LoadDepth(pixel));
    const float radius   = 0.5f * pixelCoC;
    if (radius < 0.5f) return float4(color, 1.0f);

    float3 sum       = color;
    float  weightSum = 1.0f;
    for (uint i = 0; i < SampleCount; i++)
    {
        const float  r            = sqrt((i + 0.5f) / float(SampleCount)) * radius;
        const float  theta        = i * GoldenAngle;
        const float2 offset       = r * float2(cos(theta), sin(theta));
        const int2   samplePixel  = clamp(pixel + int2(round(offset)), int2(0, 0), int2(width - 1, height - 1));
        const float  sampleRadius = CircleOfConfusion(LoadDepth(samplePixel)) * 0.5f;
        const float3 sampleColor  = _MainTexture.SampleLevel(sampler_MainTexture, input.TexCoord + offset * ubo.TexelSize, 0).rgb;
        const float  weight       = saturate(sampleRadius - r + 1.0f);

        sum       += sampleColor * weight;
        weightSum += weight;
    }

    return float4(sum / weightSum, 1.0f);
}
