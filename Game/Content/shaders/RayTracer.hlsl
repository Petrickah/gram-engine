struct Ray
{
    float3 Origin;
    float3 Direction;
};

struct Camera
{
    float3 Position     ; float TanHalfFovY;
    float3 Right        ; float Aspect     ;
    float3 Up           ; uint  SphereCount;
    float3 Forward      ; uint  FrameIndex ;
    uint   Samples      ; float SampleScale;
    uint   MaxBounces   ; float LensRadius ;   // LensRadius 0 = pinhole, everything sharp
    float  FocusDistance; float3 Padding0  ;   // distance to the sharp plane, along Forward
};

// Layout kept 1:1 with SphereData (RayTracePass.hpp): 32 bytes.
struct Sphere
{
    float3 Position   ; float  Radius       ;
    float  InnerRadius; uint   MaterialIndex; float2 Padding0;   // InnerRadius 0 = plină
};

// Layout kept 1:1 with MaterialData (RayTracePass.hpp): 32 bytes.
struct Material
{
    float3 Albedo      ; float Fuzz              ;
    float  RefractIndex; float AbsorptionDistance; uint Type; float Padding0;
};

struct HitRecord
{
    float3 Position     ; bool  FrontFace;
    float3 Normal       ; float Distance;
    uint   MaterialIndex;
};

static uint g_Seed       = 0;

// Material Types; Keep the numeric values 1:1 with C++
static const uint g_MaterialLambertian = 0;
static const uint g_MaterialMetal      = 1;
static const uint g_MaterialDielectric = 2;

float reflectance(float cosine, float refractionIndex)
{
    float r0 = (1.0f - refractionIndex) / (1.0f + refractionIndex);
    return (r0 * r0) + (1.0f - (r0 * r0)) * pow(1.0f - cosine, 5);
}

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

float3 RandomUnitVector(inout uint state)
{
    const float z   = RandomFloat(state, -1.0f, 1.0f);
    const float phi = RandomFloat(state,  0.0f, 6.28318530718f);
    const float r   = sqrt(max(0.0f, 1.0f - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

float2 RandomInUnitDisk(inout uint state)
{
    const float r   = sqrt(RandomFloat(state));
    const float phi = RandomFloat(state, 0.0f, 6.28318530718f);
    return r * float2(cos(phi), sin(phi));
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
    float2  pixelSample = (pixel + jitter) / float2(size) * 2.0f - 1.0f;
    pixelSample.y = -pixelSample.y;

    const float3 pinhole    = cam.Forward
                            + pixelSample.x * cam.Aspect * cam.TanHalfFovY * cam.Right
                            + pixelSample.y *              cam.TanHalfFovY * cam.Up
                            ;
    const float3 focusPoint = cam.Position + cam.FocusDistance * pinhole;
    const float2 lens       = cam.LensRadius * RandomInUnitDisk(g_Seed);

    Ray ray;
    ray.Origin    = cam.Position + lens.x * cam.Right + lens.y * cam.Up;
    ray.Direction = normalize(focusPoint - ray.Origin);
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

bool HitSurface(float3 center, float radius, float outwardSign, in Ray ray, float tmin, float tmax, out HitRecord rec)
{
    float3 oc     = center - ray.Origin;
    float  a      = dot(ray.Direction, ray.Direction);
    float  h      = dot(ray.Direction, oc);
    float  c      = dot(oc, oc) - radius * radius;
    float  delta  = h * h - a * c;

    if (delta < 0.0f) return false;

    float root = (h - sqrt(delta)) / a;
    if (root <= tmin || tmax <= root)
    {
        root = (h + sqrt(delta)) / a;
        if (root <= tmin || tmax <= root)
            return false;
    }

    rec.Distance  = root;
    rec.Position  = RayAt(ray, rec.Distance);
    SetFaceNormal(rec, ray.Direction, outwardSign * (rec.Position - center) / radius);
    return true;
}

bool HitSphere(in Sphere s, in Ray ray, float tmin, float tmax, out HitRecord rec)
{
    bool hit = HitSurface(s.Position, s.Radius, 1.0f, ray, tmin, tmax, rec);

    if (s.InnerRadius > 0.0f)
    {
        HitRecord inner;
        if (HitSurface(s.Position, s.InnerRadius, -1.0f, ray, tmin, hit ? rec.Distance : tmax, inner))
        {
            rec = inner;
            hit = true;
        }
    }

    if (hit) rec.MaterialIndex = s.MaterialIndex;
    return hit;
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
            float3 direction = hit.Normal + RandomUnitVector(g_Seed);
            if (dot(direction, direction) < 1e-8f) direction = hit.Normal;

            ray.Origin    = hit.Position;
            ray.Direction = normalize(direction);
            return true;
        }
        case g_MaterialMetal:
        {
            ray.Origin    = hit.Position;
            ray.Direction = normalize(reflect(ray.Direction, hit.Normal) + (mat.Fuzz * RandomUnitVector(g_Seed)));
            return dot(ray.Direction, hit.Normal) > 0;
        }
        case g_MaterialDielectric:
        {
            float ri       = hit.FrontFace ? (1.0f / mat.RefractIndex) : mat.RefractIndex;
            float cosTheta = min(dot(-ray.Direction, hit.Normal), 1.0f);
            float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

            attenuation   = 1.0f;
            ray.Origin    = hit.Position;
            ray.Direction = normalize((ri * sinTheta > 1.0f || reflectance(cosTheta, ri) > RandomFloat(g_Seed)) 
                          ? reflect(ray.Direction, hit.Normal)
                          : refract(ray.Direction, hit.Normal, ri))
                          ;
            return true;
        }
    }

    return false;
}

float3 RayColor(in Ray ray, float3 startColor, float3 endColor)
{
    float3 attenuation = 1.0f;

    for (uint bounce = 0; bounce < camera.MaxBounces; bounce++)
    {
        HitRecord hitRecord;
        if (!HitSpheres(ray, 0.01f, 3.402823466e+38f, hitRecord))
        {
            return attenuation * lerp(startColor, endColor, 0.5 * (ray.Direction.y + 1.0f));
        }

        const Material mat = materials[hitRecord.MaterialIndex];

        // Lovită din spate: raza a parcurs hitRecord.Distance prin interiorul materialului.
        if (mat.Type == g_MaterialDielectric && !hitRecord.FrontFace)
        {
            // Albedo = culoarea văzută printr-o grosime AbsorptionDistance; max: pow(0, x) e bine definit, log2(0) nu.
            attenuation *= pow(max(mat.Albedo, 1e-4f), hitRecord.Distance / mat.AbsorptionDistance);
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

    g_Seed = InitRandom(id.xy, width, camera.FrameIndex);

    float3 color = float3(0.0f, 0.0f, 0.0f);
    for (uint sampleCount = 0; sampleCount < camera.Samples; sampleCount++)
    {
        color += RayColor(CalculateRay(camera, id.xy, uint2(width, height)), float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.7f, 1.0f)) * camera.SampleScale;
    }

    result[id.xy] = float4(color, 1.0f);
}