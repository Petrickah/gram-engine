# Wave 3 — triangles and a Blender scene (plan, 2026-11-26)

A plan, not a record of what exists. Wave 2 left the ray tracer with spheres only; Wave 3 teaches it triangles, so a scene made in Blender can be loaded and rendered. Kanban: Task 3.0 and its steps 3.1–3.5. Episode 3 ("more shapes, more triangles") tells it once it works.

## The goal

Load `Game/Content/scenes/random-objects/` (8 objects, 3359 triangles, 4 materials, Blender 5.3: the `.blend`, its `.obj`/`.mtl` export with *PBR Extensions*, and a Blender render to compare against) and render it in the ray tracer, through a BVH over its triangles, with its materials.

## Not in this wave

- Rasterization (Wave 4), a render graph, a G-buffer.
- An asset manager, glTF, per-object transforms, cameras or lights from the file.
- All of Principled BSDF: only the subset below.
- The benchmark scene (a rotating model in a lit room, Task 3.x-b, `docs/wave-2-benchmarking.md` §6): it comes after, and doesn't hold Episode 3.
- A denoiser (Task 2.w) and hardware ray tracing (Task 2.z).

## The steps

Each one leaves the existing scene rendering as before; that is the first check every time.

1. **One triangle.** Möller–Trumbore in `RayTracer.hlsl`, tried on the two triangles of the `TrianglePass` quad. Works when known rays hit and miss where they should, at the right distance.
2. **The Blender scene, loaded.** An `.obj` reader for what Blender writes: `o` (one entity per object), `v`/`vn`, triangulated `f`, `usemtl`, `mtllib`. Each object becomes an entity with a `Mesh` component holding an index into a list of loaded meshes: a mesh doesn't fit a fixed-size component (the first step towards an asset manager, not one yet). The vertices are already in world space; no transforms, camera or lights. An object without `usemtl` gets a default material. Works when the 8 objects and 3359 triangles show up in the Outliner and on screen.
3. **A BVH over the triangles.** `BVHTree` reused with triangle bounds; the validator passes; the image is the same with the BVH on and off; Midpoint and SAH compared with the Profiler's recording (Task 1.x-b). SAH should matter more here than it did on the regular grid of spheres.
4. **One material for everything.** `Material.kind` replaced by the subset below, the spheres moved onto it with the same look, the scene's `.mtl` mapped onto it. Works when the Lambertian, metal and glass spheres look as before and the scene resembles Blender's render.
5. **Episode 3.** Captures, the order of the story (one triangle → the scene → the BVH and the materials), every number with its settings, then the draft through Hermes and publication.

## The material subset

| Property | `.mtl` key | If missing |
|---|---|---|
| albedo | `Kd` | 0.8 grey |
| metallic | `Pm` | 0 |
| roughness | `Pr` | from `Ns` (`Ns = (1 - roughness)^2 * 1000`), else 0.5 |
| index of refraction | `Ni` | 1.5 |
| emission | `Ke` | black |
| transmission | to be found (see below) | 0 |

The current materials fit it: Lambertian = metallic 0; Metal = metallic 1, roughness for fuzz; Dielectric = transmission 1 with the IOR. Emission is new.

What Blender's exporter does (checked on Blender 5.3, 2026-11-24): `Pr`/`Pm` only with the OBJ exporter's *PBR Extensions* option, which then drops `Ns`/`Ka` and adds `Pr`, `Pm`, `Ps`, `Pc`, `Pcr`, `aniso`, `anisor`. Without it roughness survives only in `Ns` (Gold's `Pr 0.2` was `Ns 640`) and metal as `illum 3`. The reader goes line by line, keyword first, takes the keys it knows and ignores the rest, so both exports load.

**Still unknown, check at step 4:** how Blender writes transmission (`d`, `Tf` or another key) and emission strength (folded into `Ke` or a key of its own). Export one object with Transmission 1 and one with an Emission Strength other than 1, and read the `.mtl`.

## Later, not now

- glTF when `.obj` becomes the limit: transforms, hierarchy, cameras, lights, PBR materials.
- A model that moves or rotates: its tree built once in its own space and the ray moved into that space (a bottom-level tree per model under a top-level one, what hardware ray tracing calls BLAS and TLAS), instead of rebuilding the BVH every frame.
- Analytic quads and boxes (an intersection function each, like the sphere), if the benchmark room needs them.
- Sebastian Lague's *Coding Adventure* on BVHs as a reference once triangles work: https://github.com/SebLague/Ray-Tracing (MIT, branch `Episode02`: `Assets/Scripts/BVH.cs`, `RayTracer.shader`), its optimisations measured one by one. Compare speed-ups, not his absolute FPS.
- Hardware ray tracing (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`): not on the M4 Pro (neither MoltenVK nor KosmicKrisp exposes it, checked 2026-11-10); a PC with an RTX/RDNA2+/Arc GPU (Task 2.z).
- The loader is also the bridge to Wave 4: the same vertices and indices feed the rasterizer's vertex and index buffers.
