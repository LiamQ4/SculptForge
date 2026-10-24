# Performance figures

Measurements for the v0.5 performance work ("Performance at 20-100M polygons, multithreaded brushes, fast undo").
Every number here comes from the benchmark built into SculptForge, so anyone can reproduce it:

```
sculptforge --bench [--bench-sizes 4,16,32] [--bench-dabs 40] [--bench-no-project] [--bench-out report.md]
sculptforge-cli --bench ...      # the same without the GPU (CI, servers)
```

The benchmark (core/Benchmark.cpp, app/Bench.cpp) builds a sphere the way a sculptor gets one: a quad cube
projected onto a sphere and divided (Catmull-Clark) to about 4, 16 and 33 million triangles, so the mesh has
subdivision levels and the vertex numbering of a divided mesh. It then runs a stroke with each brush, with X
symmetry on as in the app, a medium Draw Size (radius 0.12 on the unit sphere, about 64 pixels on a 1080p view
framing the model) and dabs 0.15 radius apart, and times per dab: the brush kernel (finding and moving the
vertices), the normal update, the BVH refit, the pick, and with the GPU the upload of what the dab changed
(`glFinish` included). Per stroke it times the undo record, undo and redo; then stepping subdivision levels,
saving and opening the project, and 120 frames of orbiting in a 1600 x 960 window (no vsync).

Machine: Apple M1 Pro (8 performance + 2 efficiency cores, 32 GB unified memory; 30 GB usable under Asahi
Linux), Mesa 26.1 (OpenGL 4.6). Another process kept one core busy during all runs. "Before" is main at
00c4989 with only the benchmark added (the same instrumentation, no other change); "after" is feat/perf.
GPU runs use 10 dabs per stroke (the old upload made 40 too slow), CPU-only runs 40. The 33M GPU runs skip
the project round trip: opening a second copy of a 33M project next to the first ran out of memory on this
machine before the changes.

## v0.6 (in development)

The v0.6 performance work ran on three branches at once (mesh memory, brushes, viewport), each measured
against main at aac5f68 with `sculptforge --bench --bench-sizes 4,16,33 --bench-dabs 10 --bench-no-project`, on
the same machine as below, one benchmark at a time; every figure is the better of two runs. The sections after
this one (from "Summary") are v0.5's.

### Viewport (feat/view)

"Before" is main at aac5f68 plus the benchmark rows below; "after" is feat/view. The 33M benchmark sphere has a
few cracks on main (makeCube welded badly at 26 quads per side; fixed in feat/mem), so the 33M "after" runs
use that one-line fix, as the merged branches will; without it, the 33M sphere counts as open and draws its back
faces too (last column). 104M ran with `--bench-view` (the full brush suite at that size doesn't fit in memory);
neither 64M nor 104M was drawn before (v0.5 only measured 64M on the CPU). Full reports are at the end.

| | 4M | 16M | 33M | 64M | 104M | 33M, open (no fix) |
|---|---|---|---|---|---|---|
| Orbit, 4x antialiasing (ms) | 4.8 -> **2.9** | 11.4 -> **4.9** | 20.6 -> **8.7** | **14.0** | **19.6** (51 fps) | 15.3 |
| Orbit, no antialiasing | 3.0 -> **1.6** | 8.2 -> **3.2** | 14.3 -> **6.1** | **11.3** | **16.9** (59 fps) | 12.2 |
| Orbit, Fast Navigation (no AA, motion proxy) | 3.0 -> **1.1** | 8.2 -> **1.9** | 14.3 -> **3.2** (314 fps) | **6.5** | **9.9** (101 fps) | 4.6 |
| Triangles drawn after culling (proxy) | 53% (19%) | 49% (19%) | 48% (19%) | 47% (19%) | 46% (19%) | 100% (19%) |
| First GPU upload (ms) | 87 -> **52** | 337 -> **199** | 701 -> **422** | **784** | **1283** | 571 |
| GPU memory, bytes per triangle | 46 -> **14.8** | 46 -> **14.8** | 46 -> **14.8** (1.53 -> 0.49 GB) | **14.8** | **14.8** (1.5 GB) | 14.8 |
| Motion proxy build, first move after an upload (ms) | 16 | 60 | 126 | 241 | 408 | 127 |
| GPU upload per medium dab: Standard / Mask / Paint (ms) | 0.28 / 0.39 / 0.62 -> 0.57 / 0.34 / 0.70 | 0.95 / 1.33 / 2.16 -> 1.19 / 1.05 / 1.76 | 2.19 / 2.89 / 4.64 -> **2.08 / 1.89 / 3.69** | 3.8 / 3.7 / 7.3 | 6.6 / 6.5 / 12.8 | |

"Before" GPU memory is the old buffers' size, which the old renderer didn't report: positions, normals and
curvature as floats (28 bytes per vertex, 14 per triangle), 32-bit indices (12), 32-bit polygroups (4) and, on
meshes with quads like this one, the wireframe's edges, always kept, every edge twice (16): 46 bytes per
triangle (26 without the last two). After: 12-byte vertices with about 10% copies on cluster seams (6.6 per
triangle), 16-bit indices (6), 16-bit polygroups (2), cluster tables (0.1). Polypaint adds 2.2, a mask 0.6,
the motion proxy 1.5 once the view has moved, the wireframe about 4 while it is on.

Targets from the brief: GPU memory down at least a third (down 68%, or 43% against the 26 bytes counted
before); 60 fps at 100M (101 fps with Fast Navigation, 59 without antialiasing, 51 with 4x); orbit at 33M under
about 8 ms with 4x antialiasing (8.7 ms, from 20.6: not quite); first upload at 33M under about 300 ms (422,
from 701: not reached).

#### What changed

- **Clusters** (core/UploadOrder.h). The triangles are cut into runs of 64 consecutive triangles (compact on
  any mesh whose triangle order isn't random: a divided mesh keeps each cage face's triangles together), the
  runs sorted along a Morton curve of their centres, and every 64 runs make a cluster of up to 4096 triangles.
  Each cluster has its own block-aligned range of GPU vertices, numbered by first use, so its indices are 16-bit
  with a base vertex; vertices on seams between clusters are copied into each cluster that uses them (about
  10%). The mesh's own vertex numbering is untouched. Without the Morton step, clusters on meshes with small cage
  faces (a remesh divided three times) were long strips whose normals point every way: a test sphere culled 9%
  of its clusters, 28% with it.
- **Culling** (render/Clusters.h), on the CPU every frame: clusters outside the view, and clusters whose
  triangles all face away (a normal cone per cluster against a bounding sphere). Back faces may only be skipped
  where they can't show: on a mesh that is closed and wound consistently (every directed edge has its reverse;
  found with a 64-bit hash sum while the layout is built, with a fallback that accepts unwelded seams) and
  outwards (positive signed volume), with nothing hidden, the eye outside its bounds and nothing reaching into
  the near plane's slab. Then every back face lies behind a front face, so the picture is the same; the GPU
  also culls the back faces left in clusters along the silhouette. The visible clusters go in one
  `glMultiDrawElementsBaseVertex`. About half the triangles of a sphere are drawn.
- **12-byte vertices**: the position on a 21-bit grid over the mesh's bounds plus a margin (one grid for the
  whole mesh, so a vertex copied into two clusters decodes to exactly the same point and seams can't crack; a
  stroke that leaves the grid re-sends the mesh on a new one), an octahedral normal (2 x 11 bits, within 0.12
  degrees) and the curvature (10 bits, square-root companded over its whole range, within 0.001 where the
  cavity shading ramps). Polypaint is RGBA8 with gamma 2.2 (decoded in the shader), the mask 8-bit. The
  fragment shader works out the view direction from the pixel instead of an interpolated position, so a vertex
  passes only a normal and the curvature (plus colour and mask when the mesh has them; shader variants per
  feature).
- **Motion proxy**: while a scene of 10M+ triangles moves with Fast Navigation on, each cluster of a mesh of
  256k+ triangles draws a proxy: its vertices snapped to the one nearest the centre of each cell of a grid 2.5
  average edges wide, collapsed triangles dropped, seam vertices kept, using the same vertex buffer (indices
  only, up to a quarter of the cluster's triangles; 19% in practice). The proxies of a closed mesh form a closed
  mesh (a simplicial map of it), so the same culling applies to them with their own normal cones and no
  hole can open between clusters. Proxies are built for the clusters that changed, when the view starts moving
  (126 ms at 33M after an upload, a few clusters after a stroke). Polygroup and texture display and the
  wireframe keep the full mesh.
- **Uploads**: a full upload writes the index buffer straight into the mapped buffer from the threads building
  the layout, and whole vertex arrays mapped in parallel; dab uploads gather all their runs in parallel. Each
  128-vertex block keeps a hash of what it last sent: blocks a dab marked but didn't change (paint, mask) aren't
  sent again, and clusters whose positions didn't move keep their bounds and proxy.
- **Wireframe** edges are built when the wireframe is first shown, each edge once (from the lowest-numbered
  drawn triangle using it), at the opacity two overlapping lines had.
- Per-triangle data (polygroups, texture UVs, baked-normal tangents) is found from `gl_PrimitiveID` plus the
  cluster's first drawn triangle, read through `gl_VertexID` (which includes the base vertex) from a table per
  vertex block. A start-up check draws one point to see that the driver does include it; where it doesn't, those
  meshes draw one cluster at a time with the base as a uniform (`SF_NO_VERTEXID_BASE=1` forces that path).

#### Looks the same

Every UI test's screenshots were compared pixel by pixel with main's, and the benchmark's frames too. The
viewport matches except for: outline pixels of closed meshes, where back faces used to cover a few
antialiasing samples and show as a faint light rim (now the background shows there, as it would with a closed
surface seen exactly edge-on); a few silhouette pixels moved by the position grid (a millionth of the model);
and the wireframe on quad meshes, where line ends overlap a little differently now that each edge is drawn once.
Shelf and LightBox thumbnails differ in the UI tests only because they arrive sooner.

#### Known limits

- Back faces are drawn (twice the work on huge meshes) on open meshes, with hidden polygons, when the eye is
  inside the mesh's bounds or the near plane cuts it: the cases where they can show. On a closed mesh, a lobe
  pushed through the surface so that it is inside out (winding below zero) would show a hole where its back
  faces are culled; ZBrush culls back faces always.
- The proxy is coarse on a mesh with fine detail (detail under 2.5 edges disappears while moving), and the first
  move after opening or dividing a big mesh waits for it (126 ms at 33M, 408 ms at 104M).
- 33M orbiting is 8.7 ms with antialiasing: what's left is 16M sub-pixel triangles a frame, which only a level
  of detail at rest would remove.
- The first upload at 33M (422 ms) spends most of its time building the layout (about 250 ms: two passes over
  every cluster's triangles) and packing vertices (about 100 ms, mostly curvature).
- The per-cluster fallback for drivers without the base vertex in `gl_VertexID` was checked by forcing it on
  Mesa; macOS (GL 4.1) wasn't tried on hardware.
- Upload per dab at 4M and 16M is a little slower (0.3-0.6 ms) than before: runs are finer (128 vertices) and
  the clusters a dab touches get new bounds.

## Summary

| | 4M triangles | 16M triangles | 33M triangles |
|---|---|---|---|
| Medium brush (Standard), one dab, upload included | 23.6 -> **1.3 ms** | 166 -> **5.9 ms** | 390 -> **10.3 ms** |
| Large brush (radius 0.4: 1.2M vertices per dab at 33M) | 44 -> **13 ms** | 572 -> **43 ms** | 548 -> **93 ms** |
| Vertices uploaded per medium dab (changed: 9k / 37k / 78k) | most of the index range -> **20k** | most of the range -> **60k** | most of the range -> **117k** |
| Orbit, 4x antialiasing | 5.1 -> **5.0 ms** (200 fps) | 12.7 -> **11.8 ms** (85 fps) | 30.4 -> **20.6 ms** (49 fps) |
| Orbit, Fast Navigation (no AA while moving) | **3.1 ms** | **8.0 ms** | **14.0 ms (71 fps)** |
| Undo / redo of a 40-dab medium stroke | 7.7 -> **3.4 ms** | 33 -> **12 ms** | 71 -> **24 ms** |
| Undo of a 40-dab large stroke | 28 -> **11 ms** | 116 -> **38 ms** | 286 -> **77 ms** |
| Lower Res after sculpting (folds the edits into the levels) | 300 -> **175 ms** | 1338 -> **718 ms** | 4245 -> **1539 ms** |
| Higher Res / Lower Res without edits | 2-5 -> **1-5 ms** | 5-20 -> **5-20 ms** | 37-320 -> **10-41 ms** |
| Save project | 768 -> **81 ms** | 3060 -> **321 ms** | 8115 -> **677 ms** (1.9 GB) |
| Open project | 1696 -> **550 ms** | 8130 -> **2278 ms** | 15487 -> **5679 ms** |
| BVH build (open, import, level change without cache) | 693 -> **215 ms** | 2912 -> **829 ms** | 6285 -> **1747 ms** |
| First GPU upload | 98 -> **90 ms** | 514 -> **344 ms** | 907 -> **705 ms** |
| Mesh memory (with topology) | 90 -> **62 bytes per triangle** | | 2990 -> **2062 MB** |

Targets from the brief: a dab under 16 ms at 16M triangles with a medium brush is reached (5.9 ms, and 10.3 ms
at 33M). Orbiting at 60 fps at 32M is reached while the view moves with Fast Navigation (71 fps); with 4x
antialiasing it is 49 fps on this integrated GPU (it was 33).

## What changed

- **Thread pool** (core/ThreadPool.h): a persistent pool (one thread per core, up to 16) with `poolFor`, which
  splits work into fixed blocks of 4096; sums are made per block and added in block order, so results don't
  depend on the thread count and a region of one block is computed exactly like before. Ranges under 4 blocks
  run inline. It runs every brush kernel (all types, recipes, mask, paint weights), normal updates, the BVH
  vertex query (in traversal order, so the vertex list is identical), refits and build, renderer gathers and
  project CRCs. Undo records stay serial, in order. `set_multithreading enabled=false` (or the checkbox) runs
  everything on one thread; tests compare both bit for bit.
- **Partial GPU updates.** The old renderer uploaded one index range from the lowest to the highest changed
  vertex. On a divided mesh, whose vertices are numbered level by level, a dab with symmetry covers most of
  that range: every dab re-sent nearly the whole mesh. Meshes now mark changed vertices in a bitset (plus
  2048-vertex blocks to find them fast), and the renderer stores vertices in first-use order of the triangle list
  (core/UploadOrder.h), which keeps any region of a divided mesh together; a dab uploads a few 512-vertex
  blocks (15-20 `glBufferSubData` calls per array). The same order improves the GPU's vertex cache: orbiting at
  33M went from 30 to 21 ms with the same shaders.
- **Normals** are summed per vertex from its faces (same sums as before) instead of through a per-triangle face
  normal cache, which saves 16 bytes per triangle (cache and stamps); the adjacency list no longer keeps
  spare capacity (12 bytes per triangle); the renderer no longer keeps a CPU copy of the curvature.
- **Undo** already stored only the touched vertices per stroke; it is faster because the normal and BVH updates
  it runs are. The first polypaint stroke on an unpainted mesh no longer copies the whole mesh for undo (it
  keeps the empty paint stack): at 33M that copy was over 2 GB.
- **Projects**: the ZIP writer streamed the archive into a second in-memory copy and the reader read the file
  through a byte iterator; now entries go straight to the file, are read in place, CRCs use slicing-by-8 in
  parallel, and arrays are copied (not converted byte by byte) on little-endian CPUs. Archives over 4 GB use
  ZIP64 (the old writer refused them); smaller archives are byte-identical to before.
- **Viewport**: the window has no multisampling; the 3D view draws into its own multisampled target and resolves
  it (Renderer::drawView). That allows **Fast Navigation** (scenes of 10M+ triangles drop antialiasing while the
  view moves) and falling back to fewer samples when the target can't be allocated. A mesh whose buffers run the
  GPU out of memory is skipped with a status message rather than drawn from incomplete buffers.

## Memory per triangle (33M benchmark sphere, after)

| | bytes per triangle |
|---|---|
| Mesh: positions, normals, triangles, quad pairs, polygroups, adjacency (vertex-vertex and vertex-triangle), stamps, dirty bits | 62 (was 90) |
| BVH | 19.5 |
| Subdivision levels (the level stack keeps every level's data, detail and topology) | 75 |
| GPU: positions, normals, curvature per vertex; indices | about 26 |

A 33M sculpt with levels holds about 5.4 GB (RSS); the 64M benchmark sphere 10 GB, where a medium dab still
takes 14.6 ms (without the upload) and undo of a 40-dab stroke 42 ms (last table).

## Known limits

- The subdivision level stack (75 bytes per triangle) keeps a copy of the current level's positions and
  triangles next to the working mesh; sharing them would save about 18 bytes per triangle but needs changes to
  the level code. Divide stops at 50M triangles (core/Multires.h).
- Normal updates and refits scale about 2-3x on 10 cores: they read vertices scattered over memory (the same
  numbering issue as the uploads), so they wait on memory, not arithmetic.
- Very large brushes on 33M meshes (a tenth of the model per dab) take about 90 ms per dab.
- Opening a project needs about twice the memory of the scene while it loads.

## Before (main at 00c4989 plus the benchmark)

### CPU only

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 411 |
| BVH build | 693 |
| Mesh (incl. topology) | 358 MB = 90 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 708 MB (+698 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 0.41 | 0.56 | 0.31 | 0.00 | **1.29** | 0.00 | 7.7 | 7.3 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 4.27 | 6.18 | 3.33 | 0.00 | **13.78** | 0.00 | 28.2 | 27.9 | 4.6 MB |
| Clay | 7k | 0.26 | 0.31 | 0.17 | 0.00 | **0.74** | 0.00 | 4.4 | 4.0 | 0.9 MB |
| ClayBuildup | 8k | 0.34 | 0.37 | 0.21 | 0.00 | **0.92** | 0.00 | 4.0 | 3.9 | 0.8 MB |
| Smooth | 5k | 0.21 | 0.21 | 0.11 | 0.00 | **0.53** | 0.00 | 2.6 | 2.5 | 0.6 MB |
| Inflate | 5k | 0.17 | 0.20 | 0.11 | 0.00 | **0.48** | 0.00 | 2.5 | 2.5 | 0.6 MB |
| DamStandard | 4k | 0.17 | 0.19 | 0.11 | 0.00 | **0.47** | 0.00 | 2.4 | 2.3 | 0.5 MB |
| Layer | 5k | 0.25 | 0.20 | 0.11 | 0.00 | **0.56** | 0.00 | 2.6 | 2.5 | 0.6 MB |
| Move | 4k | 0.02 | 0.10 | 0.09 | 0.00 | **0.21** | 0.00 | 0.3 | 0.3 | 0.1 MB |
| Recipe (pattern) | 5k | 0.41 | 0.20 | 0.11 | 0.00 | **0.73** | 0.00 | 2.6 | 2.4 | 0.6 MB |
| Mask | 5k | 0.15 | 0.00 | 0.00 | 0.00 | **0.15** | 0.00 | 0.0 | 0.0 | 0.3 MB |
| Paint | 5k | 0.15 | 0.00 | 0.00 | 0.00 | **0.15** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 300 |
| Higher Res (back up) | 2 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 2 |
| Save project (228 MB) | 768 |
| Open project | 1696 |

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1846 |
| BVH build | 2912 |
| Mesh (incl. topology) | 1433 MB = 90 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2778 MB (+2377 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 1.76 | 2.45 | 1.31 | 0.00 | **5.52** | 0.00 | 33.3 | 32.6 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 17.82 | 27.90 | 16.01 | 0.01 | **61.73** | 0.00 | 116.2 | 114.7 | 18.5 MB |
| Clay | 28k | 0.98 | 1.28 | 0.67 | 0.01 | **2.94** | 0.00 | 20.6 | 20.6 | 3.5 MB |
| ClayBuildup | 33k | 1.34 | 1.56 | 0.82 | 0.01 | **3.73** | 0.00 | 23.1 | 22.7 | 3.8 MB |
| Smooth | 19k | 0.86 | 0.88 | 0.44 | 0.01 | **2.19** | 0.00 | 15.5 | 15.4 | 2.7 MB |
| Inflate | 18k | 0.66 | 0.86 | 0.46 | 0.01 | **1.98** | 0.00 | 15.4 | 14.9 | 2.6 MB |
| DamStandard | 18k | 0.68 | 0.83 | 0.42 | 0.01 | **1.94** | 0.00 | 14.8 | 14.4 | 2.5 MB |
| Layer | 19k | 0.99 | 0.86 | 0.43 | 0.01 | **2.29** | 0.00 | 15.2 | 15.2 | 2.6 MB |
| Move | 16k | 0.07 | 0.41 | 0.36 | 0.01 | **0.87** | 0.00 | 1.1 | 1.1 | 0.3 MB |
| Recipe (pattern) | 19k | 1.73 | 0.89 | 0.43 | 0.01 | **3.07** | 0.00 | 15.2 | 15.2 | 2.6 MB |
| Mask | 19k | 0.57 | 0.00 | 0.00 | 0.01 | **0.58** | 0.00 | 0.3 | 0.2 | 1.3 MB |
| Paint | 19k | 0.58 | 0.00 | 0.00 | 0.01 | **0.59** | 0.00 | 0.5 | 0.3 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 1338 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 3060 |
| Open project | 8130 |

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4302 |
| BVH build | 6285 |
| Mesh (incl. topology) | 2990 MB = 90 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5749 MB (+5268 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 3.96 | 5.49 | 2.94 | 0.00 | **12.40** | 0.00 | 70.8 | 71.1 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 39.60 | 60.23 | 35.90 | 0.01 | **135.74** | 0.00 | 286.0 | 283.7 | 38.7 MB |
| Clay | 58k | 2.66 | 3.60 | 1.92 | 0.01 | **8.19** | 0.00 | 51.2 | 50.7 | 7.2 MB |
| ClayBuildup | 66k | 3.29 | 3.96 | 2.13 | 0.02 | **9.41** | 0.00 | 56.5 | 56.7 | 8.0 MB |
| Smooth | 39k | 2.58 | 2.65 | 1.32 | 0.02 | **6.58** | 0.00 | 55.5 | 40.1 | 5.7 MB |
| Inflate | 38k | 1.79 | 2.24 | 1.17 | 0.02 | **5.23** | 0.00 | 39.8 | 40.7 | 5.6 MB |
| DamStandard | 37k | 1.95 | 2.30 | 1.18 | 0.02 | **5.46** | 0.00 | 40.0 | 38.6 | 5.4 MB |
| Layer | 37k | 2.72 | 2.33 | 1.18 | 0.02 | **6.26** | 0.00 | 45.8 | 41.4 | 5.6 MB |
| Move | 33k | 0.23 | 1.27 | 1.19 | 0.02 | **2.78** | 0.00 | 3.1 | 2.9 | 0.5 MB |
| Recipe (pattern) | 38k | 4.32 | 2.29 | 1.11 | 0.02 | **7.74** | 0.00 | 37.2 | 37.8 | 5.4 MB |
| Mask | 39k | 1.23 | 0.00 | 0.00 | 0.02 | **1.25** | 0.00 | 0.7 | 0.6 | 2.7 MB |
| Paint | 39k | 1.30 | 0.00 | 0.00 | 0.02 | **1.32** | 0.00 | 1.3 | 1.1 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 4245 |
| Higher Res (back up) | 37 |
| Lower Res again (no edits) | 320 |
| Higher Res again (no edits) | 64 |
| Save project (1905 MB) | 8115 |
| Open project | 15487 |

### With the GPU, 4M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 463 |
| BVH build | 767 |
| First GPU upload | 98 |
| Mesh (incl. topology) | 358 MB = 90 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 801 MB (+730 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 0.42 | 0.52 | 0.25 | 0.00 | 22.36 | **23.57** | 0.00 | 1.9 | 1.8 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 6.78 | 9.16 | 5.13 | 0.00 | 23.38 | **44.48** | 0.00 | 28.4 | 36.0 | 4.2 MB |
| Clay | 8k | 0.75 | 0.57 | 0.27 | 0.01 | 26.43 | **28.05** | 0.00 | 1.5 | 1.5 | 0.3 MB |
| ClayBuildup | 13k | 0.64 | 0.75 | 0.34 | 0.01 | 23.57 | **25.32** | 0.00 | 2.1 | 2.1 | 0.4 MB |
| Smooth | 7k | 0.49 | 0.42 | 0.32 | 0.01 | 23.69 | **24.94** | 0.00 | 1.4 | 1.3 | 0.2 MB |
| Inflate | 7k | 0.34 | 0.40 | 0.20 | 0.01 | 21.50 | **22.45** | 0.00 | 1.3 | 1.5 | 0.2 MB |
| DamStandard | 7k | 0.35 | 0.42 | 0.18 | 0.01 | 21.82 | **22.78** | 0.00 | 1.3 | 1.4 | 0.2 MB |
| Layer | 7k | 0.64 | 0.43 | 0.19 | 0.01 | 22.58 | **23.85** | 0.00 | 1.6 | 1.7 | 0.2 MB |
| Move | 6k | 0.05 | 0.27 | 0.17 | 0.01 | 19.98 | **20.53** | 0.00 | 0.6 | 0.6 | 0.1 MB |
| Recipe (pattern) | 8k | 0.89 | 0.44 | 0.19 | 0.01 | 23.07 | **24.60** | 0.00 | 1.4 | 1.4 | 0.2 MB |
| Mask | 8k | 0.35 | 0.00 | 0.00 | 0.01 | 23.60 | **23.97** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 0.35 | 0.00 | 0.00 | 0.01 | 24.31 | **24.68** | 0.00 | 0.1 | 0.1 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 475 |
| Higher Res (back up) | 2 |
| Lower Res again (no edits) | 7 |
| Higher Res again (no edits) | 2 |
| Save project (228 MB) | 908 |
| Open project | 2026 |
| Orbit: frame time | 5.1 (194 fps) |

### With the GPU, 16M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2281 |
| BVH build | 3320 |
| First GPU upload | 514 |
| Mesh (incl. topology) | 1433 MB = 90 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2828 MB (+2757 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 1.62 | 2.14 | 1.09 | 0.01 | 161.12 | **165.99** | 0.00 | 26.0 | 16.9 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 53.96 | 87.81 | 50.14 | 0.01 | 379.68 | **571.63** | 0.00 | 124.1 | 112.5 | 17.0 MB |
| Clay | 32k | 1.45 | 1.77 | 0.86 | 0.01 | 148.08 | **152.18** | 0.00 | 6.2 | 6.1 | 1.1 MB |
| ClayBuildup | 51k | 2.45 | 2.91 | 1.54 | 0.01 | 150.24 | **157.18** | 0.01 | 8.7 | 9.2 | 1.5 MB |
| Smooth | 30k | 1.94 | 1.82 | 0.99 | 0.01 | 154.28 | **159.06** | 0.01 | 5.4 | 5.3 | 1.0 MB |
| Inflate | 29k | 1.36 | 1.68 | 0.86 | 0.01 | 152.70 | **156.62** | 0.00 | 6.2 | 6.0 | 1.0 MB |
| DamStandard | 29k | 1.39 | 1.70 | 0.81 | 0.01 | 163.71 | **167.64** | 0.01 | 6.9 | 11.5 | 1.0 MB |
| Layer | 30k | 2.48 | 1.66 | 0.77 | 0.01 | 149.94 | **154.88** | 0.00 | 5.4 | 5.4 | 1.0 MB |
| Move | 23k | 0.22 | 0.89 | 0.67 | 0.01 | 141.54 | **143.53** | 0.01 | 2.3 | 2.2 | 0.4 MB |
| Recipe (pattern) | 31k | 3.35 | 1.72 | 1.49 | 0.01 | 159.51 | **166.10** | 0.01 | 5.7 | 6.3 | 1.0 MB |
| Mask | 31k | 1.68 | 0.00 | 0.00 | 0.01 | 161.04 | **162.75** | 0.00 | 0.1 | 0.2 | 0.5 MB |
| Paint | 31k | 1.27 | 0.00 | 0.00 | 0.01 | 153.57 | **154.86** | 0.01 | 0.3 | 0.2 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 1492 |
| Higher Res (back up) | 7 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 3134 |
| Open project | 8582 |
| Orbit: frame time | 12.7 (79 fps) |

### With the GPU, 33M (no project round trip)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4091 |
| BVH build | 6244 |
| First GPU upload | 907 |
| Mesh (incl. topology) | 2990 MB = 90 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5833 MB (+5763 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 3.14 | 4.18 | 2.09 | 0.01 | 380.44 | **389.88** | 0.00 | 16.2 | 16.1 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 48.26 | 73.61 | 43.62 | 0.01 | 382.34 | **547.84** | 0.00 | 220.1 | 219.8 | 35.5 MB |
| Clay | 68k | 2.82 | 3.50 | 1.76 | 0.01 | 380.26 | **388.35** | 0.00 | 13.0 | 13.3 | 2.4 MB |
| ClayBuildup | 107k | 4.81 | 5.55 | 2.92 | 0.01 | 380.61 | **393.90** | 0.00 | 17.8 | 17.8 | 3.1 MB |
| Smooth | 63k | 3.40 | 3.27 | 1.52 | 0.01 | 379.44 | **387.64** | 0.00 | 11.5 | 11.9 | 2.1 MB |
| Inflate | 61k | 2.52 | 3.18 | 1.57 | 0.01 | 380.18 | **387.47** | 0.00 | 12.3 | 11.3 | 2.1 MB |
| DamStandard | 61k | 2.63 | 3.19 | 1.52 | 0.01 | 381.24 | **388.60** | 0.00 | 11.3 | 11.1 | 2.0 MB |
| Layer | 63k | 4.98 | 3.28 | 1.49 | 0.01 | 379.55 | **389.33** | 0.00 | 12.3 | 11.6 | 2.1 MB |
| Move | 49k | 0.39 | 1.73 | 1.43 | 0.01 | 341.09 | **345.07** | 0.00 | 4.5 | 4.5 | 0.9 MB |
| Recipe (pattern) | 65k | 6.55 | 3.40 | 1.54 | 0.01 | 380.30 | **391.80** | 0.00 | 11.3 | 11.4 | 2.1 MB |
| Mask | 66k | 2.52 | 0.00 | 0.00 | 0.01 | 382.21 | **384.75** | 0.00 | 0.3 | 0.3 | 1.0 MB |
| Paint | 66k | 2.37 | 0.00 | 0.00 | 0.01 | 389.76 | **392.16** | 0.00 | 0.5 | 0.5 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 2846 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 43 |
| Higher Res again (no edits) | 10 |
| Orbit: frame time | 30.4 (33 fps) |

## After (feat/perf)

### CPU only

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 277 |
| BVH build | 215 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 643 MB (+633 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.49 | 0.48 | 0.40 | 0.00 | **1.41** | 0.00 | 3.4 | 3.5 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.19 | 3.35 | 1.85 | 0.00 | **8.60** | 0.00 | 10.6 | 10.3 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.31 | 0.27 | 0.20 | 0.00 | **0.81** | 0.00 | 2.7 | 2.4 | 0.9 MB |
| ClayBuildup | 8k | 18k (12) | 0.40 | 0.31 | 0.25 | 0.00 | **0.99** | 0.00 | 2.4 | 2.4 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.24 | 0.17 | 0.12 | 0.00 | **0.56** | 0.00 | 1.9 | 2.0 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.19 | 0.17 | 0.12 | 0.00 | **0.51** | 0.00 | 2.0 | 2.3 | 0.6 MB |
| DamStandard | 4k | 12k (9) | 0.20 | 0.17 | 0.11 | 0.00 | **0.50** | 0.00 | 2.0 | 1.9 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.29 | 0.18 | 0.12 | 0.00 | **0.61** | 0.00 | 1.8 | 1.7 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.10 | 0.09 | 0.00 | **0.24** | 0.00 | 0.3 | 0.2 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.47 | 0.17 | 0.12 | 0.00 | **0.78** | 0.00 | 1.9 | 1.6 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.21 | 0.00 | 0.00 | 0.00 | **0.23** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | **0.19** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 175 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 81 |
| Open project | 550 |

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1140 |
| BVH build | 829 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2589 MB (+2173 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 1.99 | 1.95 | 1.15 | 0.00 | **5.25** | 0.00 | 11.6 | 11.9 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 9.76 | 10.47 | 6.11 | 0.01 | **27.04** | 0.00 | 37.6 | 37.0 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.19 | 1.15 | 0.69 | 0.01 | **3.14** | 0.00 | 7.7 | 7.6 | 3.5 MB |
| ClayBuildup | 33k | 55k (12) | 1.44 | 1.37 | 0.85 | 0.01 | **3.78** | 0.00 | 8.1 | 12.2 | 3.8 MB |
| Smooth | 19k | 37k (9) | 0.99 | 0.75 | 0.54 | 0.01 | **2.36** | 0.00 | 6.2 | 6.3 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.76 | 0.75 | 0.55 | 0.01 | **2.14** | 0.00 | 5.9 | 6.0 | 2.6 MB |
| DamStandard | 18k | 35k (9) | 0.80 | 0.73 | 0.52 | 0.01 | **2.12** | 0.00 | 5.5 | 5.9 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.06 | 0.77 | 0.51 | 0.01 | **2.42** | 0.00 | 6.0 | 6.0 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.48 | 0.55 | 0.01 | **1.26** | 0.00 | 0.8 | 1.3 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 1.71 | 0.77 | 0.51 | 0.01 | **3.06** | 0.00 | 5.8 | 5.7 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.66 | 0.00 | 0.00 | 0.01 | **0.73** | 0.00 | 0.4 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.65 | 0.00 | 0.00 | 0.01 | **0.72** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 718 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 321 |
| Open project | 2278 |

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2526 |
| BVH build | 1747 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5366 MB (+4797 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 149k (21) | 3.84 | 3.01 | 1.79 | 0.00 | **8.97** | 0.00 | 23.7 | 22.2 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 19.07 | 20.32 | 12.46 | 0.01 | **53.27** | 0.00 | 76.6 | 76.7 | 38.7 MB |
| Clay | 58k | 91k (16) | 2.26 | 2.16 | 1.08 | 0.01 | **5.72** | 0.00 | 15.3 | 15.1 | 7.2 MB |
| ClayBuildup | 66k | 102k (17) | 2.68 | 2.38 | 1.31 | 0.01 | **6.63** | 0.00 | 16.5 | 16.2 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.78 | 1.54 | 0.82 | 0.02 | **4.32** | 0.00 | 11.9 | 11.7 | 5.7 MB |
| Inflate | 38k | 66k (14) | 1.65 | 1.69 | 0.90 | 0.02 | **4.41** | 0.00 | 12.0 | 12.8 | 5.6 MB |
| DamStandard | 37k | 65k (15) | 1.65 | 1.74 | 0.84 | 0.02 | **4.41** | 0.00 | 11.8 | 11.5 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.12 | 1.74 | 0.88 | 0.02 | **4.92** | 0.00 | 12.8 | 11.8 | 5.6 MB |
| Move | 33k | 59k (10) | 0.27 | 0.77 | 0.77 | 0.02 | **1.99** | 0.00 | 1.8 | 1.8 | 0.5 MB |
| Recipe (pattern) | 38k | 68k (15) | 2.61 | 1.63 | 0.81 | 0.02 | **5.22** | 0.00 | 12.1 | 11.6 | 5.4 MB |
| Mask | 39k | 67k (15) | 1.24 | 0.00 | 0.00 | 0.02 | **1.37** | 0.00 | 1.0 | 0.7 | 2.7 MB |
| Paint | 39k | 67k (15) | 1.25 | 0.00 | 0.00 | 0.02 | **1.38** | 0.00 | 1.3 | 1.1 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1539 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 41 |
| Higher Res again (no edits) | 10 |
| Save project (1905 MB) | 677 |
| Open project | 5679 |

### With the GPU, 4M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 301 |
| BVH build | 238 |
| First GPU upload | 90 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 720 MB (+650 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 20k (12) | 0.36 | 0.36 | 0.30 | 0.00 | 0.28 | **1.33** | 0.00 | 1.2 | 1.4 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 183k (53) | 3.77 | 3.99 | 2.15 | 0.00 | 2.99 | **13.17** | 0.00 | 9.3 | 9.4 | 4.2 MB |
| Clay | 8k | 17k (11) | 0.33 | 0.31 | 0.24 | 0.00 | 0.24 | **1.15** | 0.00 | 1.6 | 1.5 | 0.3 MB |
| ClayBuildup | 13k | 24k (13) | 0.56 | 0.48 | 0.40 | 0.00 | 0.34 | **1.82** | 0.00 | 1.7 | 1.8 | 0.4 MB |
| Smooth | 7k | 16k (10) | 0.35 | 0.28 | 0.19 | 0.00 | 0.23 | **1.06** | 0.00 | 1.0 | 0.9 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.28 | 0.26 | 0.20 | 0.00 | 0.22 | **0.98** | 0.00 | 0.8 | 1.1 | 0.2 MB |
| DamStandard | 7k | 16k (10) | 0.30 | 0.26 | 0.19 | 0.00 | 0.21 | **0.98** | 0.00 | 0.8 | 1.2 | 0.2 MB |
| Layer | 7k | 16k (10) | 0.60 | 0.27 | 0.18 | 0.00 | 0.22 | **1.31** | 0.00 | 0.7 | 0.7 | 0.2 MB |
| Move | 6k | 14k (9) | 0.03 | 0.15 | 0.17 | 0.00 | 0.18 | **0.59** | 0.00 | 0.4 | 0.4 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (11) | 0.83 | 0.28 | 0.20 | 0.00 | 0.23 | **1.57** | 0.00 | 1.5 | 1.0 | 0.2 MB |
| Mask | 8k | 16k (10) | 0.30 | 0.00 | 0.00 | 0.00 | 0.39 | **0.72** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 16k (10) | 0.25 | 0.00 | 0.00 | 0.00 | 0.60 | **0.88** | 0.00 | 0.0 | 0.0 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 170 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 81 |
| Open project | 572 |
| Orbit: frame time, 4x antialiasing | 5.0 (201 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.1 (325 fps) |

### With the GPU, 16M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1206 |
| BVH build | 846 |
| First GPU upload | 344 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2670 MB (+2600 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 60k (12) | 1.75 | 1.89 | 1.10 | 0.00 | 1.04 | **5.93** | 0.00 | 3.2 | 4.2 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 650k (53) | 11.94 | 13.01 | 7.83 | 0.01 | 9.43 | **43.06** | 0.00 | 34.7 | 36.6 | 17.0 MB |
| Clay | 32k | 53k (11) | 1.48 | 1.48 | 0.82 | 0.00 | 0.87 | **4.77** | 0.00 | 2.7 | 3.3 | 1.1 MB |
| ClayBuildup | 51k | 77k (13) | 2.11 | 1.87 | 1.15 | 0.00 | 1.27 | **6.57** | 0.00 | 4.1 | 4.4 | 1.5 MB |
| Smooth | 30k | 51k (10) | 1.37 | 1.48 | 0.79 | 0.00 | 0.86 | **4.62** | 0.00 | 2.5 | 2.8 | 1.0 MB |
| Inflate | 29k | 50k (10) | 1.21 | 1.36 | 0.70 | 0.00 | 0.84 | **4.24** | 0.00 | 2.3 | 2.4 | 1.0 MB |
| DamStandard | 29k | 49k (10) | 1.18 | 1.39 | 0.76 | 0.00 | 0.81 | **4.29** | 0.00 | 2.4 | 2.6 | 1.0 MB |
| Layer | 30k | 51k (10) | 2.25 | 1.54 | 0.64 | 0.00 | 0.83 | **5.37** | 0.00 | 2.5 | 2.3 | 1.0 MB |
| Move | 23k | 42k (9) | 0.20 | 0.53 | 0.65 | 0.00 | 0.90 | **2.55** | 0.00 | 2.0 | 1.1 | 0.4 MB |
| Recipe (pattern) | 31k | 52k (11) | 2.03 | 1.41 | 0.76 | 0.00 | 0.87 | **5.20** | 0.00 | 2.4 | 2.7 | 1.0 MB |
| Mask | 31k | 52k (11) | 1.41 | 0.00 | 0.00 | 0.01 | 1.83 | **3.37** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (11) | 1.01 | 0.00 | 0.00 | 0.00 | 2.25 | **3.36** | 0.00 | 0.2 | 0.1 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 717 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 357 |
| Open project | 2279 |
| Orbit: frame time, 4x antialiasing | 11.8 (85 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.0 (125 fps) |

### With the GPU, 33M (no project round trip)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2570 |
| BVH build | 1748 |
| First GPU upload | 705 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5486 MB (+5415 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 117k (18) | 3.10 | 2.88 | 1.58 | 0.00 | 2.49 | **10.32** | 0.00 | 5.5 | 5.9 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.3M (67) | 26.54 | 27.08 | 17.16 | 0.02 | 20.26 | **92.91** | 0.00 | 69.1 | 68.2 | 35.5 MB |
| Clay | 68k | 104k (17) | 2.77 | 2.45 | 1.56 | 0.00 | 1.69 | **8.71** | 0.00 | 5.7 | 5.3 | 2.4 MB |
| ClayBuildup | 107k | 151k (21) | 3.98 | 3.02 | 1.62 | 0.01 | 2.51 | **11.46** | 0.00 | 6.5 | 6.6 | 3.1 MB |
| Smooth | 63k | 98k (17) | 2.93 | 2.28 | 1.18 | 0.01 | 1.77 | **8.39** | 0.00 | 4.5 | 5.6 | 2.1 MB |
| Inflate | 61k | 96k (16) | 2.50 | 2.17 | 1.19 | 0.01 | 1.87 | **7.97** | 0.00 | 4.8 | 4.8 | 2.1 MB |
| DamStandard | 61k | 96k (16) | 2.62 | 2.24 | 1.28 | 0.01 | 1.59 | **8.01** | 0.00 | 4.6 | 4.6 | 2.0 MB |
| Layer | 63k | 99k (17) | 4.87 | 2.34 | 1.26 | 0.01 | 1.81 | **10.57** | 0.00 | 4.7 | 4.9 | 2.1 MB |
| Move | 49k | 79k (14) | 0.42 | 0.91 | 1.05 | 0.01 | 1.65 | **4.61** | 0.00 | 2.2 | 1.9 | 0.9 MB |
| Recipe (pattern) | 65k | 102k (18) | 4.29 | 2.39 | 1.18 | 0.01 | 2.08 | **10.19** | 0.00 | 4.9 | 5.1 | 2.1 MB |
| Mask | 66k | 101k (18) | 2.52 | 0.00 | 0.00 | 0.01 | 3.03 | **5.76** | 0.01 | 0.3 | 0.2 | 1.0 MB |
| Paint | 66k | 101k (18) | 2.47 | 0.00 | 0.00 | 0.05 | 5.08 | **7.82** | 0.01 | 0.4 | 0.4 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1529 |
| Higher Res (back up) | 11 |
| Lower Res again (no edits) | 55 |
| Higher Res again (no edits) | 14 |
| Orbit: frame time, 4x antialiasing | 20.6 (49 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.0 (71 fps) |

### CPU only, 64M (no project round trip)

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4788 |
| BVH build | 3266 |
| Mesh (incl. topology) | 3953 MB = 62 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 4756 MB = 75 B/triangle |
| Process memory (RSS) | 10081 MB (+10070 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 198k | 259k (28) | 6.61 | 4.61 | 2.82 | 0.00 | **14.58** | 0.00 | 42.3 | 42.2 | 21.8 MB |
| Standard (large, r 0.4) | 1.9M | 2.0M (79) | 33.95 | 38.18 | 23.82 | 0.01 | **98.42** | 0.00 | 148.1 | 147.0 | 74.0 MB |
| Clay | 110k | 155k (21) | 4.07 | 3.18 | 1.76 | 0.02 | **9.41** | 0.00 | 27.8 | 27.2 | 13.8 MB |
| ClayBuildup | 120k | 166k (23) | 4.52 | 3.34 | 1.79 | 0.03 | **10.06** | 0.00 | 30.7 | 30.3 | 15.1 MB |
| Smooth | 71k | 108k (19) | 3.22 | 2.54 | 1.28 | 0.03 | **7.35** | 0.00 | 22.7 | 22.6 | 10.8 MB |
| Inflate | 69k | 106k (19) | 2.73 | 2.62 | 1.37 | 0.03 | **7.02** | 0.00 | 22.5 | 22.5 | 10.7 MB |
| DamStandard | 68k | 104k (18) | 2.76 | 2.60 | 1.27 | 0.03 | **6.94** | 0.00 | 22.5 | 22.5 | 10.4 MB |
| Layer | 70k | 107k (19) | 3.75 | 2.64 | 1.32 | 0.03 | **8.00** | 0.00 | 21.5 | 22.9 | 10.8 MB |
| Move | 64k | 98k (13) | 0.50 | 1.06 | 1.12 | 0.03 | **3.04** | 0.00 | 2.5 | 2.5 | 1.0 MB |
| Recipe (pattern) | 72k | 109k (19) | 4.24 | 2.53 | 1.24 | 0.03 | **8.32** | 0.00 | 22.0 | 22.7 | 10.6 MB |
| Mask | 72k | 108k (19) | 2.35 | 0.00 | 0.00 | 0.03 | **2.59** | 0.00 | 1.7 | 1.3 | 5.3 MB |
| Paint | 72k | 108k (19) | 2.41 | 0.00 | 0.00 | 0.03 | **2.65** | 0.00 | 2.6 | 2.4 | 10.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 2793 |
| Higher Res (back up) | 20 |
| Lower Res again (no edits) | 79 |
| Higher Res again (no edits) | 20 |

## v0.6 viewport reports (feat/view)

### Before: main at aac5f68 with the benchmark

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 222 |
| First GPU upload | 90 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 718 MB (+648 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 20k (12) | 0.36 | 0.36 | 0.30 | 0.00 | 0.28 | **1.33** | 0.00 | 1.3 | 1.3 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 183k (53) | 3.70 | 4.00 | 2.07 | 0.00 | 2.85 | **12.88** | 0.00 | 9.3 | 9.5 | 4.2 MB |
| Clay | 8k | 17k (11) | 0.33 | 0.30 | 0.24 | 0.00 | 0.23 | **1.13** | 0.00 | 1.0 | 0.9 | 0.3 MB |
| ClayBuildup | 13k | 24k (13) | 0.54 | 0.44 | 0.40 | 0.00 | 0.32 | **1.73** | 0.00 | 2.1 | 2.2 | 0.4 MB |
| Smooth | 7k | 16k (10) | 0.34 | 0.26 | 0.19 | 0.00 | 0.22 | **1.03** | 0.00 | 0.8 | 1.4 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.28 | 0.25 | 0.20 | 0.00 | 0.21 | **0.96** | 0.00 | 0.6 | 1.6 | 0.2 MB |
| DamStandard | 7k | 16k (10) | 0.29 | 0.25 | 0.19 | 0.00 | 0.21 | **0.96** | 0.00 | 1.0 | 2.1 | 0.2 MB |
| Layer | 7k | 16k (10) | 0.59 | 0.30 | 0.20 | 0.00 | 0.22 | **1.34** | 0.00 | 1.3 | 2.1 | 0.2 MB |
| Move | 6k | 14k (9) | 0.03 | 0.15 | 0.18 | 0.00 | 0.18 | **0.59** | 0.00 | 0.4 | 0.4 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (11) | 0.72 | 0.27 | 0.20 | 0.00 | 0.22 | **1.44** | 0.00 | 0.7 | 1.4 | 0.2 MB |
| Mask | 8k | 16k (10) | 0.30 | 0.00 | 0.00 | 0.00 | 0.39 | **0.71** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 16k (10) | 0.25 | 0.00 | 0.00 | 0.00 | 0.65 | **0.93** | 0.00 | 0.0 | 0.0 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 184 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Orbit: frame time, 4x antialiasing | 5.0 (199 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.2 (308 fps) |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1189 |
| BVH build | 794 |
| First GPU upload | 337 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2712 MB (+2172 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 60k (12) | 1.55 | 1.65 | 1.09 | 0.00 | 0.95 | **5.35** | 0.00 | 3.1 | 3.1 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 650k (53) | 11.12 | 11.62 | 7.06 | 0.01 | 9.93 | **40.64** | 0.00 | 30.7 | 30.4 | 17.0 MB |
| Clay | 32k | 53k (11) | 1.31 | 1.46 | 0.87 | 0.00 | 0.83 | **4.58** | 0.00 | 2.2 | 2.6 | 1.1 MB |
| ClayBuildup | 51k | 77k (13) | 1.87 | 1.54 | 1.01 | 0.00 | 1.20 | **5.76** | 0.00 | 3.1 | 3.0 | 1.5 MB |
| Smooth | 30k | 51k (10) | 1.17 | 1.18 | 0.54 | 0.00 | 1.02 | **4.02** | 0.00 | 2.3 | 2.4 | 1.0 MB |
| Inflate | 29k | 50k (10) | 1.24 | 1.30 | 0.86 | 0.00 | 0.76 | **4.25** | 0.00 | 2.5 | 2.4 | 1.0 MB |
| DamStandard | 29k | 49k (10) | 1.27 | 1.38 | 0.66 | 0.00 | 0.99 | **4.42** | 0.00 | 3.0 | 2.7 | 1.0 MB |
| Layer | 30k | 51k (10) | 2.29 | 1.42 | 0.59 | 0.00 | 1.06 | **5.49** | 0.00 | 2.9 | 3.2 | 1.0 MB |
| Move | 23k | 42k (9) | 0.19 | 0.64 | 0.66 | 0.00 | 0.63 | **2.37** | 0.00 | 1.6 | 1.8 | 0.4 MB |
| Recipe (pattern) | 31k | 52k (11) | 1.69 | 1.20 | 0.50 | 0.00 | 0.80 | **4.31** | 0.00 | 2.0 | 2.5 | 1.0 MB |
| Mask | 31k | 52k (11) | 1.29 | 0.00 | 0.00 | 0.01 | 1.44 | **2.82** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (11) | 0.98 | 0.00 | 0.00 | 0.00 | 2.64 | **3.71** | 0.00 | 0.2 | 0.1 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 766 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 21 |
| Higher Res again (no edits) | 5 |
| Orbit: frame time, 4x antialiasing | 11.8 (84 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.7 (115 fps) |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2469 |
| BVH build | 1655 |
| First GPU upload | 701 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5642 MB (+4820 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 117k (18) | 3.34 | 3.86 | 1.84 | 0.00 | 2.62 | **11.91** | 0.00 | 6.9 | 5.6 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.3M (67) | 21.60 | 22.79 | 14.70 | 0.01 | 17.56 | **78.51** | 0.00 | 63.7 | 62.8 | 35.5 MB |
| Clay | 68k | 104k (17) | 2.63 | 2.26 | 1.34 | 0.00 | 1.76 | **8.20** | 0.00 | 4.4 | 4.6 | 2.4 MB |
| ClayBuildup | 107k | 151k (21) | 4.31 | 2.84 | 1.68 | 0.01 | 2.87 | **12.06** | 0.00 | 6.0 | 6.3 | 3.1 MB |
| Smooth | 63k | 98k (17) | 2.55 | 1.91 | 1.05 | 0.01 | 1.70 | **7.46** | 0.00 | 4.3 | 4.6 | 2.1 MB |
| Inflate | 61k | 96k (16) | 2.36 | 2.27 | 1.31 | 0.01 | 1.67 | **7.82** | 0.00 | 4.4 | 4.0 | 2.1 MB |
| DamStandard | 61k | 96k (16) | 2.30 | 1.85 | 1.15 | 0.01 | 1.42 | **6.92** | 0.00 | 4.2 | 3.9 | 2.0 MB |
| Layer | 63k | 99k (17) | 4.53 | 2.18 | 1.14 | 0.01 | 1.60 | **9.66** | 0.00 | 3.7 | 3.8 | 2.1 MB |
| Move | 49k | 79k (14) | 0.39 | 0.83 | 1.02 | 0.01 | 1.39 | **4.17** | 0.00 | 2.1 | 1.7 | 0.9 MB |
| Recipe (pattern) | 65k | 102k (18) | 4.06 | 2.12 | 1.13 | 0.01 | 1.84 | **9.39** | 0.00 | 4.0 | 4.3 | 2.1 MB |
| Mask | 66k | 101k (18) | 2.54 | 0.00 | 0.00 | 0.01 | 2.89 | **5.63** | 0.00 | 0.3 | 0.2 | 1.0 MB |
| Paint | 66k | 101k (18) | 2.19 | 0.00 | 0.00 | 0.01 | 4.64 | **7.03** | 0.00 | 0.4 | 0.3 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1588 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Orbit: frame time, 4x antialiasing | 20.6 (49 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.8 (68 fps) |

### After: feat/view (33M with the makeCube fix from feat/mem)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 227 |
| First GPU upload | 52 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 721 MB (+650 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 21k (14) | 0.40 | 0.43 | 0.30 | 0.00 | 0.58 | **1.79** | 0.00 | 1.1 | 1.1 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 191k (39) | 3.58 | 3.34 | 2.04 | 0.01 | 2.89 | **12.78** | 0.00 | 9.3 | 8.1 | 4.2 MB |
| Clay | 8k | 18k (10) | 0.38 | 0.36 | 0.25 | 0.00 | 0.52 | **1.58** | 0.00 | 1.1 | 1.2 | 0.3 MB |
| ClayBuildup | 13k | 25k (15) | 0.68 | 0.55 | 0.42 | 0.00 | 0.57 | **2.33** | 0.00 | 1.8 | 1.4 | 0.4 MB |
| Smooth | 7k | 17k (10) | 0.36 | 0.30 | 0.19 | 0.00 | 0.47 | **1.38** | 0.00 | 0.9 | 1.6 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.34 | 0.34 | 0.21 | 0.00 | 0.48 | **1.44** | 0.00 | 0.9 | 0.7 | 0.2 MB |
| DamStandard | 7k | 16k (10) | 0.40 | 0.35 | 0.21 | 0.00 | 0.48 | **1.50** | 0.00 | 1.4 | 1.1 | 0.2 MB |
| Layer | 7k | 17k (10) | 0.69 | 0.34 | 0.20 | 0.00 | 0.55 | **1.85** | 0.00 | 1.0 | 1.2 | 0.2 MB |
| Move | 6k | 12k (10) | 0.05 | 0.17 | 0.18 | 0.00 | 0.40 | **0.87** | 0.00 | 0.5 | 0.6 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (10) | 0.95 | 0.41 | 0.25 | 0.00 | 0.50 | **2.18** | 0.00 | 1.3 | 1.2 | 0.2 MB |
| Mask | 8k | 17k (10) | 0.37 | 0.00 | 0.00 | 0.01 | 0.42 | **0.87** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 17k (10) | 0.49 | 0.00 | 0.00 | 0.01 | 0.70 | **1.29** | 0.00 | 0.1 | 0.3 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 175 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Motion proxy build (first move after an upload) | 16 |
| Orbit: frame time, 4x antialiasing | 2.9 (347 fps) |
| Orbit: frame time, no antialiasing | 1.7 (605 fps) |
| Orbit: triangles drawn | 2.1M (53%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 1.1 (932 fps) |
| Orbit: triangles drawn (motion proxy) | 771k (19%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1145 |
| BVH build | 787 |
| First GPU upload | 200 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2786 MB (+2286 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 61k (25) | 1.47 | 1.23 | 0.87 | 0.00 | 1.19 | **5.10** | 0.00 | 2.9 | 2.5 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 699k (74) | 10.90 | 11.26 | 7.20 | 0.01 | 10.53 | **43.38** | 0.00 | 31.2 | 30.8 | 17.0 MB |
| Clay | 32k | 54k (21) | 1.22 | 1.28 | 0.73 | 0.00 | 1.03 | **4.54** | 0.00 | 2.1 | 2.5 | 1.1 MB |
| ClayBuildup | 51k | 78k (25) | 2.37 | 1.92 | 1.04 | 0.01 | 1.39 | **7.14** | 0.00 | 3.2 | 4.0 | 1.5 MB |
| Smooth | 30k | 51k (19) | 1.36 | 1.29 | 0.75 | 0.00 | 0.97 | **4.63** | 0.00 | 2.2 | 2.1 | 1.0 MB |
| Inflate | 29k | 50k (19) | 1.21 | 1.21 | 0.63 | 0.01 | 0.96 | **4.26** | 0.00 | 2.5 | 2.3 | 1.0 MB |
| DamStandard | 29k | 50k (19) | 1.15 | 1.18 | 0.59 | 0.01 | 1.06 | **4.23** | 0.00 | 2.3 | 1.9 | 1.0 MB |
| Layer | 30k | 51k (20) | 2.01 | 1.10 | 0.52 | 0.01 | 0.86 | **4.74** | 0.00 | 2.3 | 2.3 | 1.0 MB |
| Move | 23k | 40k (11) | 0.21 | 0.38 | 0.45 | 0.01 | 0.72 | **2.13** | 0.00 | 0.9 | 1.0 | 0.4 MB |
| Recipe (pattern) | 31k | 53k (19) | 1.76 | 1.25 | 0.55 | 0.01 | 1.14 | **4.98** | 0.00 | 2.9 | 2.4 | 1.0 MB |
| Mask | 31k | 52k (19) | 1.26 | 0.00 | 0.00 | 0.01 | 1.08 | **2.60** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (19) | 1.12 | 0.00 | 0.00 | 0.01 | 1.80 | **3.17** | 0.00 | 0.2 | 0.2 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 748 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 25 |
| Higher Res again (no edits) | 7 |
| Motion proxy build (first move after an upload) | 68 |
| Orbit: frame time, 4x antialiasing | 4.9 (202 fps) |
| Orbit: frame time, no antialiasing | 3.2 (317 fps) |
| Orbit: triangles drawn | 7.8M (49%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 1.9 (525 fps) |
| Orbit: triangles drawn (motion proxy) | 3.1M (19%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 3030 |
| BVH build | 1693 |
| First GPU upload | 461 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5757 MB (+4345 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 114k (35) | 2.95 | 2.73 | 1.55 | 0.00 | 2.35 | **10.25** | 0.00 | 6.4 | 5.0 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.4M (104) | 22.30 | 24.75 | 15.38 | 0.01 | 22.64 | **92.39** | 0.00 | 67.3 | 68.8 | 35.5 MB |
| Clay | 68k | 101k (30) | 2.67 | 2.49 | 1.42 | 0.01 | 2.07 | **9.26** | 0.00 | 5.4 | 4.9 | 2.4 MB |
| ClayBuildup | 107k | 150k (35) | 4.00 | 3.00 | 1.58 | 0.01 | 2.77 | **12.20** | 0.00 | 6.2 | 6.9 | 3.1 MB |
| Smooth | 63k | 95k (28) | 2.88 | 2.22 | 1.10 | 0.01 | 2.17 | **8.93** | 0.00 | 5.2 | 5.3 | 2.1 MB |
| Inflate | 61k | 92k (28) | 2.52 | 2.68 | 1.34 | 0.01 | 1.88 | **8.98** | 0.00 | 4.8 | 4.8 | 2.1 MB |
| DamStandard | 61k | 93k (27) | 2.35 | 2.42 | 1.22 | 0.01 | 1.90 | **8.46** | 0.00 | 4.4 | 4.3 | 2.0 MB |
| Layer | 63k | 96k (27) | 4.21 | 1.98 | 0.97 | 0.01 | 1.72 | **9.48** | 0.00 | 4.6 | 5.0 | 2.1 MB |
| Move | 49k | 77k (15) | 0.39 | 1.13 | 1.32 | 0.01 | 1.51 | **5.16** | 0.00 | 2.1 | 2.1 | 0.9 MB |
| Recipe (pattern) | 65k | 99k (28) | 4.60 | 2.42 | 1.15 | 0.01 | 2.19 | **10.95** | 0.00 | 5.0 | 5.8 | 2.1 MB |
| Mask | 66k | 97k (28) | 2.43 | 0.00 | 0.00 | 0.01 | 1.89 | **4.87** | 0.00 | 0.3 | 0.3 | 1.0 MB |
| Paint | 66k | 97k (28) | 2.33 | 0.00 | 0.00 | 0.01 | 3.69 | **6.58** | 0.00 | 0.5 | 0.5 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1557 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 41 |
| Higher Res again (no edits) | 10 |
| Motion proxy build (first move after an upload) | 142 |
| Orbit: frame time, 4x antialiasing | 8.7 (115 fps) |
| Orbit: frame time, no antialiasing | 6.1 (163 fps) |
| Orbit: triangles drawn | 15.9M (48%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 3.2 (314 fps) |
| Orbit: triangles drawn (motion proxy) | 6.4M (19%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

### After: feat/view, 33M without the makeCube fix (the sphere has cracks, so it counts as open)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2463 |
| BVH build | 1653 |
| First GPU upload | 571 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5497 MB (+5427 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 114k (35) | 3.14 | 2.73 | 1.44 | 0.00 | 2.25 | **10.23** | 0.00 | 5.5 | 5.5 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.4M (104) | 22.73 | 22.99 | 14.65 | 0.01 | 21.52 | **89.23** | 0.00 | 64.1 | 63.2 | 35.5 MB |
| Clay | 68k | 101k (30) | 3.13 | 2.15 | 1.24 | 0.01 | 1.95 | **9.06** | 0.00 | 4.9 | 5.0 | 2.4 MB |
| ClayBuildup | 107k | 150k (35) | 4.85 | 2.71 | 1.74 | 0.01 | 2.74 | **13.00** | 0.00 | 7.2 | 7.1 | 3.1 MB |
| Smooth | 63k | 95k (28) | 2.82 | 1.77 | 1.03 | 0.01 | 1.80 | **7.96** | 0.00 | 4.7 | 4.7 | 2.1 MB |
| Inflate | 61k | 92k (28) | 2.89 | 1.96 | 1.22 | 0.01 | 1.83 | **8.43** | 0.00 | 4.2 | 4.8 | 2.1 MB |
| DamStandard | 61k | 93k (27) | 2.80 | 1.97 | 1.17 | 0.01 | 1.82 | **8.32** | 0.00 | 4.1 | 4.1 | 2.0 MB |
| Layer | 63k | 96k (27) | 4.58 | 2.05 | 1.07 | 0.01 | 1.83 | **10.10** | 0.01 | 4.5 | 4.1 | 2.1 MB |
| Move | 49k | 77k (15) | 0.41 | 0.79 | 1.01 | 0.01 | 1.40 | **4.43** | 0.00 | 2.4 | 2.1 | 0.9 MB |
| Recipe (pattern) | 65k | 99k (28) | 3.28 | 1.91 | 1.01 | 0.01 | 2.04 | **8.85** | 0.00 | 4.3 | 4.4 | 2.1 MB |
| Mask | 66k | 97k (28) | 2.58 | 0.00 | 0.00 | 0.01 | 2.04 | **5.21** | 0.01 | 0.4 | 0.3 | 1.0 MB |
| Paint | 66k | 97k (28) | 2.84 | 0.00 | 0.00 | 0.01 | 3.67 | **7.11** | 0.01 | 0.8 | 1.0 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1541 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Motion proxy build (first move after an upload) | 127 |
| Orbit: frame time, 4x antialiasing | 15.3 (66 fps) |
| Orbit: frame time, no antialiasing | 12.2 (82 fps) |
| Orbit: triangles drawn | 33.2M (100%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 4.6 (218 fps) |
| Orbit: triangles drawn (motion proxy) | 6.5M (19%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

### After: feat/view, 64M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4596 |
| BVH build | 3209 |
| First GPU upload | 784 |
| GPU memory (buffers) | 940 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 3953 MB = 62 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 4756 MB = 75 B/triangle |
| Process memory (RSS) | 10407 MB (+10337 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 149k | 204k (46) | 5.29 | 3.40 | 2.23 | 0.00 | 3.79 | **16.17** | 0.00 | 10.5 | 10.2 | 5.3 MB |
| Standard (large, r 0.4) | 2.4M | 2.7M (141) | 42.11 | 43.62 | 28.13 | 0.01 | 40.24 | **167.87** | 0.00 | 125.7 | 123.4 | 67.9 MB |
| Clay | 129k | 180k (40) | 4.91 | 3.28 | 1.88 | 0.01 | 3.40 | **14.67** | 0.00 | 9.4 | 10.1 | 4.5 MB |
| ClayBuildup | 204k | 271k (46) | 7.79 | 4.76 | 2.66 | 0.01 | 4.79 | **21.48** | 0.00 | 11.7 | 12.6 | 6.0 MB |
| Smooth | 120k | 167k (37) | 4.84 | 3.12 | 1.42 | 0.01 | 3.01 | **13.37** | 0.01 | 10.0 | 10.1 | 4.0 MB |
| Inflate | 117k | 164k (37) | 4.65 | 3.14 | 1.93 | 0.01 | 3.13 | **13.81** | 0.01 | 8.1 | 8.1 | 3.9 MB |
| DamStandard | 116k | 165k (37) | 4.61 | 3.07 | 1.62 | 0.01 | 3.27 | **13.70** | 0.00 | 7.5 | 7.7 | 3.9 MB |
| Layer | 120k | 169k (39) | 8.03 | 3.36 | 1.44 | 0.01 | 3.09 | **16.93** | 0.01 | 10.5 | 11.0 | 4.0 MB |
| Move | 93k | 130k (29) | 0.74 | 1.46 | 1.60 | 0.01 | 2.43 | **7.76** | 0.00 | 3.7 | 3.8 | 1.6 MB |
| Recipe (pattern) | 125k | 175k (40) | 7.13 | 3.07 | 1.37 | 0.02 | 3.43 | **16.15** | 0.01 | 10.4 | 10.4 | 4.0 MB |
| Mask | 126k | 174k (39) | 4.59 | 0.00 | 0.00 | 0.02 | 3.66 | **9.19** | 0.00 | 0.6 | 0.6 | 2.0 MB |
| Paint | 126k | 174k (39) | 4.64 | 0.00 | 0.00 | 0.01 | 7.28 | **12.88** | 0.00 | 0.9 | 0.9 | 4.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 2842 |
| Higher Res (back up) | 19 |
| Lower Res again (no edits) | 77 |
| Higher Res again (no edits) | 19 |
| Motion proxy build (first move after an upload) | 241 |
| Orbit: frame time, 4x antialiasing | 14.0 (71 fps) |
| Orbit: frame time, no antialiasing | 11.3 (88 fps) |
| Orbit: triangles drawn | 29.9M (47%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 6.5 (154 fps) |
| Orbit: triangles drawn (motion proxy) | 12.4M (19%) |
| GPU memory with the motion proxy | 1214 MB = 19.1 B/triangle |

### After: feat/view, 104M (--bench-view)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 104.0M triangles (52.0M vertices, 8 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 7638 |
| BVH build | 5301 |
| First GPU upload | 1283 |
| GPU memory (buffers) | 1535 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 6455 MB = 62 B/triangle |
| BVH | 2043 MB = 19.6 B/triangle |
| Subdivision levels (stack) | 7765 MB = 75 B/triangle |
| Process memory (RSS) | 16656 MB (+16586 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 244k | 321k (59) | 8.15 | 5.73 | 3.85 | 0.01 | 6.60 | **26.45** | 0.00 | 17.3 | 17.0 | 8.6 MB |
| Mask | 237k | 309k (58) | 8.68 | 0.00 | 0.00 | 0.01 | 6.54 | **17.23** | 0.00 | 1.2 | 1.2 | 4.2 MB |
| Paint | 237k | 309k (58) | 8.79 | 0.00 | 0.00 | 0.01 | 12.77 | **23.43** | 0.00 | 2.0 | 1.9 | 8.4 MB |

| Operation | ms |
|---|---:|
| Motion proxy build (first move after an upload) | 408 |
| Orbit: frame time, 4x antialiasing | 19.6 (51 fps) |
| Orbit: frame time, no antialiasing | 16.9 (59 fps) |
| Orbit: triangles drawn | 48.1M (46%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 9.9 (101 fps) |
| Orbit: triangles drawn (motion proxy) | 20.1M (19%) |
| GPU memory with the motion proxy | 1982 MB = 19.1 B/triangle |
