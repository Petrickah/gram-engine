struct Ray
{
    float3 Origin;
    float3 Direction;
};

struct Camera
{
    float3 Position; float TanHalfFovY;
    float3 Right   ; float Aspect     ;
    float3 Up      ; uint  SphereCount;
    float3 Forward ; uint  FrameIndex ;
    uint   Samples ; float SampleScale;
};

struct Sphere
{
    float3 Position     ; float  Radius  ;
    uint   MaterialIndex; float3 Padding0;
};

struct Material
{
    float3 Albedo; float Fuzz;
    uint   Type  ; uint3 Padding0;
};

struct HitRecord
{
    float3 Position     ; bool  FrontFace;
    float3 Normal       ; float Distance;
    uint   MaterialIndex;
};

static uint g_MaxBounces = 0;
static uint g_Seed       = 0;

// Material Types; Keep the numeric values 1:1 with C++
static const uint g_MaterialLambertian = 0;
static const uint g_MaterialMetal      = 1;

uint PcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(inout uint state)
{
    state = PcgHash(state);
    return float(state >> 8) * (1.0f / 16777216.0f);
}

float RandomFloat(inout uint state, float lo, float hi)
{
    return lo + (hi - lo) * RandomFloat(state);
}

float3 RandomFloat3(inout uint state)
{
    const float z   = RandomFloat(state, -1.0f, 1.0f);
    const float phi = RandomFloat(state,  0.0f, 6.28318530718f);
    const float r   = sqrt(max(0.0f, 1.0f - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

uint InitRandom(uint2 pixel, uint width, uint frame)
{
    return PcgHash(pixel.y * width + pixel.x) ^ PcgHash(frame);
}

[[vk::binding(0, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> result;
[[vk::binding(1, 0)]] ConstantBuffer<Camera>     camera;
[[vk::binding(2, 0)]] StructuredBuffer<Sphere>   spheres;
[[vk::binding(3, 0)]] StructuredBuffer<Material> materials;

Ray CalculateRay(in Camera cam, uint2 pixel, uint2 size)
{
    const float2 jitter = float2(RandomFloat(g_Seed), RandomFloat(g_Seed));
    float3  pixelSample = float3(pixel + jitter, 0.0f) / float3(size, 1.0f) * 2.0f - 1.0f;
    pixelSample.y = -pixelSample.y;

    Ray ray;
    ray.Origin    = cam.Position;
    ray.Direction = normalize(cam.Forward
                            + pixelSample.x * cam.Aspect * cam.TanHalfFovY * cam.Right
                            + pixelSample.y *              cam.TanHalfFovY * cam.Up);
    return ray;
}

float3 RayAt(in Ray ray, float distance)
{
    return ray.Origin + distance * ray.Direction;
}

void SetFaceNormal(inout HitRecord rec, float3 direction, float3 normal)
{
    rec.FrontFace = dot(direction, normal) < 0;
    rec.Normal    = rec.FrontFace ? normal : -normal;
}

bool HitSphere(in Sphere s, in Ray ray, float tmin, float tmax, out HitRecord rec)
{
    float3 oc     = s.Position - ray.Origin;
    float  a      = dot(ray.Direction, ray.Direction);
    float  h      = dot(ray.Direction, oc);
    float  c      = dot(oc, oc) - s.Radius * s.Radius;
    float  delta  = h * h - a * c;

    if (delta < 0.0f) return false;

    float root = (h - sqrt(delta)) / a;
    if (root <= tmin || tmax <= root)
    {
        root = (h + sqrt(delta)) / a;
        if (root <= tmin || tmax <= root)
            return false;
    }

    rec.Distance      = root;
    rec.Position      = RayAt(ray, rec.Distance);
    rec.MaterialIndex = s.MaterialIndex;
    SetFaceNormal(rec, ray.Direction, normalize((rec.Position - s.Position) / s.Radius));
    return true;
}

bool HitSpheres(in Ray ray, float tmin, float tmax, out HitRecord hitRecord)
{
    HitRecord temp;
    bool hitAnything = false;
    float hitClosest = tmax;

    for (uint index = 0; index < camera.SphereCount; index++)
    {
        if (HitSphere(spheres[index], ray, tmin, hitClosest, temp))
        {
            hitAnything = true;
            hitClosest  = temp.Distance;
            hitRecord   = temp;
        }
    }

    return hitAnything;
}

bool Scatter(in Material mat, inout Ray ray, in HitRecord hit, out float3 attenuation)
{
    attenuation = mat.Albedo;

    switch (mat.Type)
    {
        case g_MaterialLambertian:
        {
            float3 direction = hit.Normal + RandomFloat3(g_Seed);
            if (dot(direction, direction) < 1e-8f) direction = hit.Normal;

            ray.Origin    = hit.Position;
            ray.Direction = normalize(direction);
            return true;
        }
        case g_MaterialMetal:
        {
            ray.Origin    = hit.Position;
            ray.Direction = normalize(reflect(ray.Direction, hit.Normal) + (mat.Fuzz * RandomFloat3(g_Seed)));
            return dot(ray.Direction, hit.Normal) > 0;
        }
    }

    return false;
}

float3 RayColor(in Ray ray, float3 startColor, float3 endColor)
{
    float3 attenuation = 1.0f;

    for (uint bounce = 0; bounce < g_MaxBounces; bounce++)
    {
        HitRecord hitRecord;
        if (!HitSpheres(ray, 0.01f, 3.402823466e+38f, hitRecord))
        {
            return attenuation * lerp(startColor, endColor, 0.5 * (ray.Direction.y + 1.0f));
        }

        float3 materialAttenuation;
        if (!Scatter(materials[hitRecord.MaterialIndex], ray, hitRecord, materialAttenuation)) return 0.0f;
        attenuation *= materialAttenuation;
    }

    return 0.0f;
}

[shader("compute")]
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    result.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    g_MaxBounces = 10;
    g_Seed       = InitRandom(id.xy, width, camera.FrameIndex);

    float3 color = float3(0.0f, 0.0f, 0.0f);
    for (uint sampleCount = 0; sampleCount < camera.Samples; sampleCount++)
    {
        color += RayColor(CalculateRay(camera, id.xy, uint2(width, height)), float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.7f, 1.0f)) * camera.SampleScale;
    }

    result[id.xy] = float4(color, 1.0f);
}