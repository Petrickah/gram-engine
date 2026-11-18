# Wave 2 — a BVH over the spheres (spec, step 1)

Step 1 of Task 2.x: the ray tracer stops testing every sphere for every ray. Steps 2 and 3 (a triangle in the ray tracer, then an `.obj` loader with a BVH over triangles) reuse everything built here, so nothing below is sphere-specific except two small functions: the box around one primitive, and the test against it.

**Baseline to beat:** the book's scene, 486 spheres, ~8 FPS on the M4 Pro (1 sample, 10 bounces, twice the viewport's resolution). Every ray tests all 486 spheres at every bounce.

**Who writes what:** the BVH itself (build, traversal, slab test) is yours. The `.pass.ron` change, the buffer and the UI toggle are small; do them too, or ask.

## The idea in one paragraph

Put a box (AABB: a min and a max corner) around all the spheres. Split them in two groups, put a box around each group, and keep splitting until a group holds 1 or 2 spheres. That's a tree of boxes. A ray that misses a box can't hit anything inside it, so it skips the whole branch. Instead of 486 sphere tests, a ray does about 2 · log₂(486) ≈ 18 box tests plus a few sphere tests.

## 1. The node, on both sides

One more buffer in `RayTracer.pass.ron`, and the node count in the uniform:

```ron
buffers: [
    ...,
    Buffer(name: "nodes", element: "BvhNode", fields: [
        Field(name: "min",           ty: Vec3),
        Field(name: "left_or_first", ty: U32),   // interior: the left child's index; leaf: the first sphere
        Field(name: "max",           ty: Vec3),
        Field(name: "count",         ty: U32),   // 0 = interior node; > 0 = a leaf with this many spheres
    ]),
],
```

and in `uniform`: `Field(name: "node_count", ty: U32)` and `Field(name: "use_bvh", ty: U32)` (the toggle, see 5).

32 bytes per node, the generator lays it out with no padding. Rules:

- **Node 0 is the root.**
- **Interior node** (`count == 0`): its children are `left_or_first` and `left_or_first + 1`, always next to each other, so one index is enough.
- **Leaf** (`count > 0`): its spheres are `Spheres[left_or_first .. left_or_first + count)`. The build **reorders the sphere array** so every leaf's spheres are contiguous. Nothing else breaks: each sphere carries its own `MaterialIndex`.

A tree with N spheres has at most 2N − 1 nodes: create the nodes buffer with `2 * m_SphereCount` elements (1024).

## 2. The build (C++, CPU)

New files `Game/Runtime/src/Bvh.hpp` / `Bvh.cpp`:

```cpp
// Builds a BVH over `spheres`, reordering them so each leaf's spheres are contiguous. Node 0 is the root.
std::vector<BvhNode> BuildBvh(std::vector<SphereData>& spheres);
```

Called in `RayTracePass::PackScene`, after `m_Spheres` is filled, only when something changed (the scene rarely changes; with `bob` playing it changes every frame, and that's fine: 486 spheres build in well under a millisecond).

Algorithm (top-down, midpoint split, recursive on the CPU is fine):

1. Reserve `2 * spheres.size()` nodes; push the root covering `[0, spheres.size())`.
2. For a node covering `[first, first + count)`:
   - its box = the union of its spheres' boxes; one sphere's box is `center ± radius` on every axis;
   - if `count <= 2`: it's a leaf (`left_or_first = first`, `count = count`), stop;
   - else pick the **longest axis** of the box and the split position = the middle of the box on that axis;
   - partition `spheres[first .. first + count)` so the ones whose **center** is below the split come first (`std::partition`);
   - if one side ended up empty (all centers on one side), split at the middle **index** instead (`count / 2`), or the recursion never ends;
   - append two child nodes (left, then right: adjacent), set this node's `left_or_first` to the left one's index and `count = 0`, recurse into both.

Write a box helper struct (`Aabb { glm::vec3 Min, Max; void Grow(const Aabb&); }`) on the C++ side only; the GPU only sees `BvhNode`.

## 3. The traversal (HLSL)

Two functions in `RayTracer.hlsl`, then `HitSpheres` uses them:

```hlsl
// The slab test: the ray hits the box if the intervals where it's inside each pair of planes overlap.
// invDirection = 1 / ray.Direction, computed once per ray (a 0 component gives ±inf, which works).
bool HitBox(float3 boxMin, float3 boxMax, float3 origin, float3 invDirection, float tmin, float tmax);

// Closest hit through the tree: a fixed-size stack instead of recursion (HLSL has none).
bool HitSpheresBvh(in Ray ray, float tmin, float tmax, out HitRecord hitRecord);
```

`HitBox`:
```
t0 = (boxMin - origin) * invDirection
t1 = (boxMax - origin) * invDirection
near = max of min(t0, t1) over x, y, z, and tmin
far  = min of max(t0, t1) over x, y, z, and tmax
hit when near <= far
```

`HitSpheresBvh`:
```
stack[32], top = 0; push 0 (the root)
hitClosest = tmax
while the stack isn't empty:
    node = Nodes[pop]
    if !HitBox(node, ray, tmin, hitClosest): continue      ← hitClosest, not tmax: boxes behind the closest hit are skipped
    if node.Count > 0: test spheres First .. First + Count with HitSphere, as HitSpheres does today
    else: push node.LeftOrFirst + 1, then node.LeftOrFirst
```

32 levels is plenty for 486 spheres (a balanced tree is ~9 deep). Guard the push anyway (`if (top < 32)`): a stack overflow on the GPU doesn't crash, it reads garbage.

`HitSpheres` becomes: `return Params.UseBvh != 0 ? HitSpheresBvh(...) : the current loop;`. Both callers (`RayColor`, `PrimaryDepth`) stay as they are.

## 4. Upload

Like the spheres: a per-frame `StorageBuffer<BvhNode>` (`m_NodeStorages`), written in `Record` from the vector `BuildBvh` returned, its descriptor at `RayTracerParams::NodesBinding`. `NodeCount` in the uniform = the vector's size.

## 5. Tests

There's no C++ test framework in the game, so the checks are a validator in Debug plus what you see.

**A. `ValidateBvh(nodes, spheres)`**, called after every build in Debug (`GRAM_CONFIG_DEBUG`), logging an error on the first problem:
- every sphere index is in exactly one leaf (count them: each must be seen once);
- every node's box contains its children's boxes (interior) or its spheres' boxes (leaf);
- every child index is `< nodes.size()`.

Expected: no error, on the book's scene and on 1, 2 and 3 spheres (the small cases are where off-by-ones live).

**B. Same image.** A checkbox "Use BVH" in the Ray Tracer panel drives `UseBvh`. Toggling it on the book's scene must give the **same** image once accumulated: a BVH changes which spheres are tested, never which one is hit. Any difference (a missing sphere, a hole) is a bug in the build or the traversal.

**C. Faster.** The FPS in the menu bar, BVH on vs off, on the book's scene. Expected: several times the ~8 FPS baseline. The ground (radius 1000) makes the root box huge, so every ray still enters the tree; that's fine.

**D. Optional heat map.** A `debug_view` uniform field: when set, `CSMain` writes the number of box + sphere tests of the primary ray as a color instead of the image (e.g. `tests / 64.0` in red). Without a BVH it's uniformly bright; with one, dark everywhere except around the clusters of spheres. Worth it for the devlog.

## Pitfalls

- **Rays with a 0 direction component:** `1.0 / 0.0 = inf` makes the slab test still work; don't add an epsilon.
- **`tmax` in the box test must shrink** to the closest hit so far; with the original `tmax`, the tree is walked but nothing is skipped (correct image, no speed-up: a classic).
- **Reordering:** after `BuildBvh`, `m_Spheres` is in tree order. `PackScene`'s change detection compares against the previous frame's packed array; compare the arrays from **before** the build, or a stable scene reports a change every frame (the build's order is deterministic, so comparing after works too, but only if the input order is).
- **The stack size** is a constant in the shader and a depth limit for the build: if you ever see missing geometry with a lot of objects, check the depth first.
