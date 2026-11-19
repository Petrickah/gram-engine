#pragma once

#ifdef GRAM_VULKAN_RENDERER

struct BVHNode;
struct SphereData;

class BVHTree
{
public:
    struct AABBBox
    {
        glm::vec3 Min = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 Max = glm::vec3(0.0f, 0.0f, 0.0f);

        AABBBox Grow(const AABBBox& other) { return AABBBox { .Min = glm::min(Min, other.Min), .Max = glm::max(Max, other.Max) }; }
    };

    struct Stats
    {
        uint32_t    Nodes = 0, Leaves = 0;
        uint32_t    MaxDepth = 0;          // the root is depth 0
        uint32_t    MaxLeafSize = 0;       // spheres in the fullest leaf
        float       BuildMs = 0.0f;        // set by whoever times Create
        std::string Error;                 // empty = valid; else the first problem found
    };

    // Walks the whole tree once: checks it against `spheres` (the array Create reordered) and counts it.
    Stats Validate(std::span<const SphereData> spheres) const;

public:
    BVHTree() = default;
    static BVHTree Create(std::span<SphereData> spheres);

    const std::vector<BVHNode>&  GetNodes() const;
    const BVHNode& operator[](size_t index) const;
          BVHNode& operator[](size_t index);

private:
    BVHTree(size_t size);
    AABBBox Cover(std::span<const SphereData> spheres, size_t first, size_t count);

    void Split(std::span<SphereData> spheres, size_t nodeIndex, size_t first, size_t count);
    void Visit(std::span<const SphereData> spheres, std::vector<uint32_t>& seen, Stats& stats, uint32_t index, uint32_t depth) const;

private:
    std::vector<BVHNode> m_Nodes;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
