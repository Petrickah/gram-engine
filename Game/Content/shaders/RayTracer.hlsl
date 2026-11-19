#include "RayTracer.generated.hlsli"

struct Ray
{
    float3 Origin;
    float3 Direction;
};

struct HitRecord
{
    float3 Position     ; bool  FrontFace;
    float3 Normal       ; float Distance;
    uint   MaterialIndex;
};

static uint g_Seed  = 0;
static uint g_Tests = 0;

static const float g_Miss               = 3.402823466e+38f;
// Material Types; Keep the numeric values 1:1 with C++
static const uint  g_MaterialLambertian = 0;
static const uint  g_MaterialMetal      = 1;
static const uint  g_MaterialDielectric = 2;

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


Ray CalculateRay(in RayTracerParams cam, uint2 pixel, uint2 size)
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

float HitBox(float3 boxMin, float3 boxMax, float3 origin, float3 invDirection, float tmin, float tmax)
{
    g_Tests++;

    const float3 t0 = (boxMin - origin) * invDirection;   // where the ray crosses each axis' min plane
    const float3 t1 = (boxMax - origin) * invDirection;   // ... and each axis' max plane

    const float3 enter = min(t0, t1);   // per axis: the earlier crossing is the entry
    const float3 exit  = max(t0, t1);   // ... the later one the exit

    const float near = max(max(enter.x, enter.y), max(enter.z, tmin));   // the last entry
    const float far  = min(min(exit.x,  exit.y),  min(exit.z,  tmax));   // the first exit
    return near <= far ? near : g_Miss;
}

bool HitSphere(in SphereData s, in Ray ray, float tmin, float tmax, out HitRecord rec)
{
    g_Tests++;
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

bool HitSpheresBvh(in Ray ray, float tmin, float tmax, out HitRecord hitRecord)
{
    hitRecord = (HitRecord)0;
    if (Params.NodeCount == 0) return false;   // an empty scene has no root

    const float3 invDirection = 1.0f / ray.Direction;
    bool  hitAnything = false;
    float hitClosest  = tmax;

    for (uint index = 0; index < Params.LargeCount; index++)
    {
        HitRecord temp;
        if (HitSphere(Spheres[index], ray, tmin, hitClosest, temp))
        {
            hitAnything = true;
            hitClosest  = temp.Distance;
            hitRecord   = temp;
        }
    }

    uint top = 0;
    uint stack[32];
    stack[top++] = 0;   // the root
    while (top > 0)     // while there is something on the stack
    {
        const BVHNode node = Nodes[stack[--top]];

        // hitClosest, not tmax: a box entirely behind the closest hit so far can't hold a closer one.
        if (HitBox(node.Min, node.Max, ray.Origin, invDirection, tmin, hitClosest) == g_Miss) continue;

        if (node.Count > 0)   // a leaf: test its spheres, like HitSpheres does
        {
            for (uint index = node.First; index < node.First + node.Count; index++)
            {
                HitRecord temp;
                if (HitSphere(Spheres[Params.LargeCount + index], ray, tmin, hitClosest, temp))
                {
                    hitAnything = true;
                    hitClosest  = temp.Distance;
                    hitRecord   = temp;
                }
            }
        }
        else if (top + 2 <= 32)   // an interior node: walk both children (guarded: the stack is fixed)
        {
            const BVHNode left    = Nodes[node.First];
            const BVHNode right   = Nodes[node.First + 1];
            const float   toLeft  = HitBox(left.Min,  left.Max,  ray.Origin, invDirection, tmin, hitClosest);
            const float   toRight = HitBox(right.Min, right.Max, ray.Origin, invDirection, tmin, hitClosest);

            // The farther one pushed first: the nearer is popped next, and a hit in it may make the farther one skippable.
            const bool  leftIsNearer = toLeft <= toRight;
            const float nearDist = leftIsNearer ? toLeft  : toRight;
            const float farDist  = leftIsNearer ? toRight : toLeft;
            const uint  nearNode = leftIsNearer ? node.First : node.First + 1;
            const uint  farNode  = leftIsNearer ? node.First + 1 : node.First;

            if (farDist  != g_Miss && top < 32) stack[top++] = farNode;
            if (nearDist != g_Miss && top < 32) stack[top++] = nearNode;
        }
    }

    return hitAnything;
}

bool HitSpheres(in Ray ray, float tmin, float tmax, out HitRecord hitRecord)
{
    HitRecord temp;
    bool hitAnything = false;
    float hitClosest = tmax;

    if (Params.UseBvh != 0) return HitSpheresBvh(ray, tmin, tmax, hitRecord);

    for (uint index = 0; index < Params.SphereCount; index++)
    {
        if (HitSphere(Spheres[index], ray, tmin, hitClosest, temp))
        {
            hitAnything = true;
            hitClosest  = temp.Distance;
            hitRecord   = temp;
        }
    }

    return hitAnything;
}

bool Scatter(in MaterialData mat, inout Ray ray, in HitRecord hit, out float3 attenuation)
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

    for (uint bounce = 0; bounce < Params.MaxBounces; bounce++)
    {
        HitRecord hitRecord;
        if (!HitSpheres(ray, 0.01f, 3.402823466e+38f, hitRecord))
        {
            return attenuation * lerp(startColor, endColor, 0.5 * (ray.Direction.y + 1.0f));
        }

        const MaterialData mat = Materials[hitRecord.MaterialIndex];

        // Lovită din spate: raza a parcurs hitRecord.Distance prin interiorul materialului.
        if (mat.Type == g_MaterialDielectric && !hitRecord.FrontFace)
        {
            // Albedo = culoarea văzută printr-o grosime AbsorptionDistance; max: pow(0, x) e bine definit, log2(0) nu.
            attenuation *= pow(max(mat.Albedo, 1e-4f), hitRecord.Distance / mat.AbsorptionDistance);
        }

        float3 materialAttenuation;
        if (!Scatter(Materials[hitRecord.MaterialIndex], ray, hitRecord, materialAttenuation)) return 0.0f;
        attenuation *= materialAttenuation;
    }

    return 0.0f;
}

float PrimaryDepth(in RayTracerParams cam, uint2 pixel, uint2 size)
{
    float2 ndc = (pixel + 0.5f) / float2(size) * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    Ray ray;
    ray.Origin    = cam.Position;
    ray.Direction = normalize(cam.Forward + ndc.x * cam.Aspect * cam.TanHalfFovY * cam.Right
                                          + ndc.y *              cam.TanHalfFovY * cam.Up);

    HitRecord hit;
    if (!HitSpheres(ray, 0.01f, 3.402823466e+38f, hit)) return 1e6f;
    return hit.Distance * dot(ray.Direction, cam.Forward);
}

[shader("compute")]
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Result.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    g_Seed = InitRandom(id.xy, width, Params.FrameIndex);

    float3 viewColor = float3(0.0f, 0.0f, 0.0f);
    float  viewDepth = PrimaryDepth(Params, id.xy, uint2(width, height));

    if (Params.DebugView != 0)
    {
        // The primary ray's cost: PrimaryDepth just ran one HitSpheres for this pixel, counted in g_Tests.
        const float heat = saturate(float(g_Tests) / 64.0f);    // 64 tests and above = full red
        Result[id.xy] = float4(heat, 0.0f, 1.0f - heat, 1.0f);  // blue = cheap, red = expensive
        return;
    }

    Depth[id.xy] = viewDepth;

    for (uint sampleCount = 0; sampleCount < Params.Samples; sampleCount++)
    {
        viewColor += RayColor(CalculateRay(Params, id.xy, uint2(width, height)), float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.7f, 1.0f)) * Params.SampleScale;
    }

    Result[id.xy] = float4(viewColor, 1.0f);
}