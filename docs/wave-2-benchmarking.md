# Benchmarking the ray tracer (plan, 2026-11-24)

A plan, not a record of what exists. It comes out of Wave 2's numbers, three of which couldn't be compared with anything: the book's scene at ~8 FPS was measured while the scene's resolution followed the editor's Viewport panel, whose size nobody wrote down; the Legion Go S read ~15 FPS in Debug with an unknown power profile; and a capture showed the FSR 1 checkbox on at a render scale where FSR doesn't run. A benchmark here means a measurement without those unknowns, repeatable on the M4 Pro and on the Legion Go S (through `gram deploy`) alike.

Kanban: Task 1.x-b (the profiler's pass toggles and recording) and Task 2.x-b (the benchmark scene).

## 1. Pin everything that changes the result, and write it down

- **Resolution**: a fixed one from `Debug/Display`, never *Fit the viewport*.
- **V-Sync off**, or the monitor's refresh caps the numbers (50% and 67% render scale once looked identical at 175 FPS).
- **Build configuration**: the same every time, Release by default.
- **Scene and camera**: the same file, the same seed (`std::mt19937(1337)` for the book's scene), the same camera path.
- **Quality settings**: samples per frame, max bounces, render scale, BVH strategy. Record what actually ran, not what's ticked: FSR 1 only runs below 100% (`RayTracePass::UsesFsr`).
- **The machine**: GPU, driver (MoltenVK or KosmicKrisp on the Mac; RADV on the Legion Go), OS, and on the Legion Go the power profile / TDP.

The recording (section 5) writes all of this into the file's header, so a number never travels without its settings.

## 2. Measure time, not frame rate

- **GPU ms per pass** from the profiler (timestamp queries) is the measurement; FPS mixes GPU, CPU and presentation.
- **A unit independent of resolution** for comparing machines: rays per second (pixels × samples × bounces reached / time), or ms per million pixels.
- **Many frames, not one**: a warm-up (a few seconds), then the mean or median over a few hundred frames, plus the slowest frame. The profiler's panel smooths its values; a benchmark needs the raw per-frame ones.
- **Accumulation**: a moving scene restarts it every frame, which is what a benchmark wants (the cost of one frame). A still scene keeps accumulating; the cost per frame stays the same, but say which one was measured.

## 3. What to measure besides the frame time

- **BVH build time** on the CPU, separately from the traversal: with triangles it grows far more than with 486 spheres, and a rotating model may rebuild it every frame (section 6).
- **The tree**: nodes, depth, fullest leaf (the validator has them), and `valid`.
- **Tests per ray**: boxes and primitives, mean and maximum. The heat map counts them already; the benchmark needs them as numbers.
- **One table per model**: no BVH, Midpoint, SAH. On a regular grid of spheres SAH only won a little (46 → 49 FPS); uneven triangle geometry is where it should show.

## 4. Check the image, not only the speed

- The same image with and without the BVH: a *Take Picture* per variant, compared pixel by pixel (a fixed seed, accumulation off or the same sample count).
- The validator's result next to every number.
- **A baseline before each change**: the naive version first (every ray against every triangle), measured with the same settings. The ~8 FPS of Wave 2 was that baseline, minus the settings.

## 5. The profiler: pass toggles and recording

- **A toggle per pass** (Task 1.x-b): a checkbox that skips a pass's `Record`, to see what each pass costs and contributes (the ray tracer's raw output without Accumulate, for one).
- **Record**: a button in `Engine/Profiler` that starts and stops writing samples to a file, at an interval set in the panel (for example 0.5 s or 1 s).
  - **One row per interval**: the elapsed time; per pass, the mean GPU ms over the interval; the frame's mean ms; the mean FPS over the interval (frames counted / interval length); the slowest frame in the interval.
  - **Format**: CSV, one file per recording, in `benchmarks/` next to the executable (like `screenshots/`), named by date and time. The settings from section 1 go into a small JSON file next to it with the same name (or `#` comment lines at the top of the CSV; JSON is easier to read from Python).
  - **Raw per-frame values** can be a second CSV, for percentiles; the interval file is what the graph uses.
- **A Python script** (`tools/` or next to the files) reads one or more recordings and draws, against time: ms per pass (stacked or as lines) and the mean FPS per interval, with the settings in the title. Several recordings on one graph compare variants (no BVH / Midpoint / SAH, Mac / Legion Go). matplotlib is enough.

## 6. The benchmark scene

A model rotating around its own axis, inside a room made of boxes, lit by an emissive surface: in effect a Cornell box with a test model in it. A rotating model and an enclosed room make every frame different (no accumulation) and every ray bounce off walls instead of escaping to the sky.

What it needs, roughly in order:
- **Triangles and the `.obj` loader** (Task 2.x, steps 2–3).
- **Boxes for the room**: either quads/boxes as analytic shapes (an intersection function each, like the sphere; noted in Task 2.x) or plain triangles.
- **An emissive material** (a new `Material.kind`): a surface that adds light where a ray hits it. In a closed room the sky no longer lights anything, so the emissive panel is the light. Expect much more noise than under the sky: few random paths find a small light (sampling the light directly is the usual answer, later).
- **Rotation**: either rebuild the model's BVH every frame (simple; its CPU cost becomes part of what's measured) or build it once in the model's own space and move each ray into that space with the inverse transform (a bottom-level tree per model under a top-level one: BLAS/TLAS, noted in Task 2.x). The benchmark can compare the two.
- **The model**: the Stanford dragon is the classic choice, but its scans run from tens of thousands to almost a million triangles: start with a reduced one, and treat the triangle count as a setting of its own. Check the license of the exact file used (the Stanford 3D Scanning Repository and the archives redistributing its models each state their own terms) before it goes anywhere public; until then it lives in `Game/Content/third-party/`, git-ignored.
