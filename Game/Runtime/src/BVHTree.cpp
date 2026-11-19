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

BVHTree BVHTree::Create(std::span<SphereData> spheres, Strategy strategy)
{
    BVHTree tree(2 * spheres.size());
    tree.m_Strategy = strategy;
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
    const AABBBox bbox = Cover(spheres, first, count);
    m_Nodes[nodeIndex].Min = bbox.Min;
    m_Nodes[nodeIndex].Max = bbox.Max;

    if (count <= 2)    // a leaf: its spheres are [first, first + count)
    {
        m_Nodes[nodeIndex].First = static_cast<uint32_t>(first);
        m_Nodes[nodeIndex].Count = static_cast<uint32_t>(count);
        return;
    }

    int   axis = 0;
    float cut  = 0.0f;
    const bool split = (m_Strategy == Strategy::Sah) ? SahCut(spheres, first, count, bbox, axis, cut)
                                                     : MidpointCut(bbox, axis, cut);
    if (!split)   // cheaper as a leaf (SAH only): all its spheres, tested one by one
    {
        m_Nodes[nodeIndex].First = static_cast<uint32_t>(first);
        m_Nodes[nodeIndex].Count = static_cast<uint32_t>(count);
        return;
    }

    // 3. centers below the cut first, the rest after (std::partition: quicksort's partition step)
    auto begin  = spheres.begin() + first;
    auto middle = std::partition(begin, begin + count, [&](const SphereData& s) -> bool { return s.Position[axis] < cut; });
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

// The middle of the box's longest axis: always cuts (a node whose centers all land on one side is cut by
// index in Split).
bool BVHTree::MidpointCut(const AABBBox& box, int& axis, float& cut)
{
    const glm::vec3 extent = box.Max - box.Min;
    axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    cut = 0.5f * (box.Min[axis] + box.Max[axis]);
    return true;
}

bool BVHTree::SahCut(std::span<const SphereData> spheres, size_t first, size_t count, const AABBBox& nodeBox, int& outAxis, float& outCut)
{
    // Every sphere goes into one of 8 equal slices ("bins") of the centers' range, per axis; each of the 7
    // planes between slices is a candidate cut, priced by SAH: a child's cost = its surface area (how likely
    // a ray enters it) x its sphere count (what it then tests), relative to this node's area.
    constexpr int bins = 8;
    float bestCost = static_cast<float>(count);   // making this node a leaf: test all its spheres
    int   bestAxis = -1;
    float bestCut  = 0.0f;

    for (int axis = 0; axis < 3; axis++)
    {
        float lo = std::numeric_limits<float>::max(), hi = -lo;   // the range of the centers on this axis
        for (size_t i = first; i < first + count; i++) { lo = std::min(lo, spheres[i].Position[axis]); hi = std::max(hi, spheres[i].Position[axis]); }
        if (hi - lo < 1e-6f) continue;                            // every center in one place: no cut on this axis

        std::array<AABBBox, bins> box;   box.fill(AABBBox::EmptyBox());
        std::array<uint32_t, bins> n {};
        for (size_t i = first; i < first + count; i++)
        {
            const int b = std::min(bins - 1, static_cast<int>((spheres[i].Position[axis] - lo) / (hi - lo) * bins));
            box[b] = box[b].Grow(SphereToAABB(spheres[i]));
            n[b]++;
        }

        for (int cut = 1; cut < bins; cut++)   // the plane between slice cut - 1 and slice cut
        {
            AABBBox left = AABBBox::EmptyBox(), right = AABBBox::EmptyBox();
            uint32_t nl = 0, nr = 0;
            for (int b = 0;   b < cut;  b++) { if (n[b]) { left  = left.Grow(box[b]);  nl += n[b]; } }
            for (int b = cut; b < bins; b++) { if (n[b]) { right = right.Grow(box[b]); nr += n[b]; } }
            if (nl == 0 || nr == 0) continue;

            const float cost = 1.0f + (AABBBox::Area(left) * nl + AABBBox::Area(right) * nr) / AABBBox::Area(nodeBox);   // 1 = the cost of visiting the node
            if (cost < bestCost) { bestCost = cost; bestAxis = axis; bestCut = lo + (hi - lo) * cut / bins; }
        }
    }

    outAxis = bestAxis;
    outCut  = bestCut;
    return bestAxis >= 0;   // no cut cheaper than testing everything: keep it a leaf
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
