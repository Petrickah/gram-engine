#include "BVHTree.hpp"
#include "Passes.generated.hpp"

#ifdef GRAM_VULKAN_RENDERER

static BVHTree::AABBBox SphereToAABB(const SphereData& sphere)
{
    const glm::vec3 c = sphere.Position;
    const float     r = sphere.Radius;

    return BVHTree::AABBBox
    {
        .Min = glm::vec3(c.x - r, c.y - r, c.z - r),
        .Max = glm::vec3(c.x + r, c.y + r, c.z + r),
    };
}

static bool Inside(glm::vec3 innerMin, glm::vec3 innerMax, const BVHNode& outer)
{
    return glm::all(glm::lessThanEqual(outer.Min, innerMin)) && glm::all(glm::lessThanEqual(innerMax, outer.Max));
}

BVHTree BVHTree::Create(std::span<SphereData> spheres)
{
    BVHTree tree(2 * spheres.size());
    if (spheres.empty()) return tree;

    tree.m_Nodes.emplace_back();
    tree.Split(spheres, 0, 0, spheres.size());
    return tree;
}

// Visits node `index` (at `depth`) and everything under it: counts into `stats`, marks each sphere a leaf
// holds in `seen`, and stops at the first problem (written to stats.Error).
void BVHTree::Visit(std::span<const SphereData> spheres, std::vector<uint32_t>& seen, BVHTree::Stats& stats, uint32_t index, uint32_t depth) const
{
    if (!stats.Error.empty()) return;   // a problem was already found elsewhere: stop walking

    const BVHNode& node = m_Nodes[index]; stats.Nodes++;

    if (node.Count > 0)    // a leaf: stop here
    {
        stats.Leaves++;
        stats.MaxDepth    = std::max(stats.MaxDepth,    depth);
        stats.MaxLeafSize = std::max(stats.MaxLeafSize, node.Count);

        // Its spheres are [First, First + Count): that range must exist...
        if (node.First + node.Count > spheres.size())
        {
            stats.Error = std::format("leaf {} points past the last sphere", index);
            return;
        }

        for (uint32_t sphere = node.First; sphere < node.First + node.Count; sphere++)
        {
            // ...each sphere must fit in the leaf's box...
            const AABBBox box = SphereToAABB(spheres[sphere]);
            if (!Inside(box.Min, box.Max, node))
            {
                stats.Error = std::format("sphere {} is not inside leaf {}", sphere, index);
                return;
            }

            // ...and is counted, to find the ones in no leaf or in two (checked in Validate).
            seen[sphere]++;
        }

        return;
    }

    // An interior node: both children must exist, and fit in its box.
    if (node.First + 1 >= m_Nodes.size()) { stats.Error = std::format("node {}: a child index past the end", index); return; }

    for (uint32_t child = node.First; child <= node.First + 1; child++)
    {
        if (!Inside(m_Nodes[child].Min, m_Nodes[child].Max, node))
        {
            stats.Error = std::format("node {} is not inside its parent {}", child, index);
            return;
        }
    }

    Visit(spheres, seen, stats, node.First + 0, depth + 1);
    Visit(spheres, seen, stats, node.First + 1, depth + 1);
}

BVHTree::Stats BVHTree::Validate(std::span<const SphereData> spheres) const
{
    BVHTree::Stats stats;
    std::vector<uint32_t> seen(spheres.size(), 0);   // seen[i] = how many leaves hold sphere i
    if (!m_Nodes.empty()) Visit(spheres, seen, stats, 0, 0);

    // Every sphere in exactly one leaf: 0 = lost from the tree, 2 = in two leaves.
    for (size_t sphere = 0; sphere < seen.size() && stats.Error.empty(); sphere++)
    {
        if (seen[sphere] != 1) stats.Error = std::format("sphere {} is in {} leaves", sphere, seen[sphere]);
    }

    return stats;
}

// Fills node `nodeIndex`, which covers spheres [first, first + count): a leaf, or two children appended.
void BVHTree::Split(std::span<SphereData> spheres, size_t nodeIndex, size_t first, size_t count)
{
    const AABBBox box = Cover(spheres, first, count);
    m_Nodes[nodeIndex].Min = box.Min;
    m_Nodes[nodeIndex].Max = box.Max;

    if (count <= 2)    // a leaf: its spheres are [first, first + count)
    {
        m_Nodes[nodeIndex].First = static_cast<uint32_t>(first);
        m_Nodes[nodeIndex].Count = static_cast<uint32_t>(count);
        return;
    }

    // 1. the longest axis: 0, 1 or 2
    const glm::vec3 extent = box.Max - box.Min;

    int axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    
    // 2. where to cut: the middle of the box on that axis
    const float split = 0.5f * (box.Min[axis] + box.Max[axis]);

    // 3. centers below the cut first, the rest after (std::partition: quicksort's partition step)
    auto begin  = spheres.begin() + first;
    auto middle = std::partition(begin, begin + count, [&](const SphereData& s) -> bool { return s.Position[axis] < split; });
    uint32_t leftCount = static_cast<uint32_t>(middle - begin);
    if (leftCount == 0 || leftCount == count) leftCount = count / 2;   // all centers on one side: cut by index

    // 4. two children, next to each other, then fill them
    const uint32_t left = static_cast<uint32_t>(m_Nodes.size());
    m_Nodes.emplace_back();
    m_Nodes.emplace_back();
    m_Nodes[nodeIndex].First = left;   // interior: First = the left child, the right one is First + 1
    m_Nodes[nodeIndex].Count = 0;

    Split(spheres, left + 0, first + 0,         leftCount + 0);
    Split(spheres, left + 1, first + leftCount, count - leftCount);
}

BVHTree::AABBBox BVHTree::Cover(std::span<const SphereData> spheres, size_t first, size_t count)
{
    BVHTree::AABBBox aabb = SphereToAABB(spheres[first]);

    for (size_t it = first; it < first + count - 1; ++it)
    {
        aabb = aabb.Grow(SphereToAABB(spheres[it + 1]));
    }

    return aabb;
}

BVHTree::BVHTree(size_t size) { m_Nodes.reserve(size); }
const std::vector<BVHNode>& BVHTree::GetNodes()  const { return m_Nodes;        }
const BVHNode& BVHTree::operator[](size_t index) const { return m_Nodes[index]; }
      BVHNode& BVHTree::operator[](size_t index)       { return m_Nodes[index]; }

#endif // #ifdef GRAM_VULKAN_RENDERER
