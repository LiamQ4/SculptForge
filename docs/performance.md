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

# v0.6

## Memory per triangle (feat/mem)

Target from the brief: the whole scene under ~60 bytes per triangle at the top level, Divide to 100M+, opening a
33M project in under ~2 s without a 2x peak, and no slower dabs, undo or level stepping. Measured with the same
benchmark (it now also reports the process memory per triangle, the peak while dividing, and the memory and peak
of opening the project). Another agent's builds and benchmarks shared the machine, so every number is the better
of two runs, before and after alternating.

### Summary (33M benchmark sphere)

| | before (main at aac5f68) | after (feat/mem) |
|---|---|---|
| Scene memory (CPU benchmark, process RSS) | 5.3 GB = 159 bytes per triangle | **3.0 GB = 90 bytes per triangle** |
| Sculpting mesh (with its adjacency) | 62 B/triangle | **39** |
| Subdivision level stack | 75 B/triangle | **29** |
| BVH (feat/brush's) | 19.5 B/triangle | 19.5 |
| Biggest Divide on this machine (30 GB) | 50M triangles (fixed limit) | **about 160M** (104M measured: 9.0 GB, 11 GB peak while dividing) |
| Divide up to 33M (the benchmark's setup) | 2428 ms | **1415 ms** |
| Open project | 4721 ms, the whole 1.9 GB file read into memory first | **2137 ms**, peak 1.15x the scene (+3.5 GB, peak +4.0 GB) |
| Save project | 663 ms, 1.9 GB file | **433 ms, 1.37 GB** |
| Lower Res after sculpting | 1543 ms | **876 ms** |
| Higher / Lower Res without edits | 10 / 40 ms | 8-13 / 43-55 ms |
| Medium dab (Standard), GPU upload included | 11.1 ms | 13.7 ms (CPU-only runs: 9.16 -> 9.09 ms) |
| Large dab (radius 0.4) | 62.6 ms | 64.8 ms |
| Smooth / Clay dab | 5.47 / 6.57 ms | 5.77 / 7.02 ms |
| Undo of a medium / large stroke | 21.8 / 69.1 ms | 25.5 / 70.0 ms (CPU-only: 21.4 -> 21.6 ms) |
| Orbit, 4x antialiasing | 19.7 ms | 20.0 ms |

At 16M the dabs are 0-20% slower (Standard 7.79 -> 7.69 ms, Clay 4.05 -> 4.68, Smooth 2.98 -> 3.50, Mask 1.50 ->
1.71): the rings and triangle lists of implicit adjacency take a few nanoseconds more per vertex than reading
stored lists, which shows in the normal update, the BVH refit, Smooth and the renderer's curvature (part of the
GPU upload). Undo is the same or faster.

### What changed

- **Generated levels** (core/Multires.h, `SubdivTopology::generated`): a level made by Divide numbers its
  vertices as coarse vertices, then edge points, then face points, and lays its triangles out polygon by polygon
  (`generateFaces`). Such a level stores no triangles or quad pairs (in `SubdivLevel::data`), no child maps and no
  per-triangle parents and corner codes; all of them follow from the coarse polygons. The working mesh gets its
  triangles generated when the level is entered. Levels from Reconstruct Subdiv, imports and older projects whose
  numbering matches are recognised as generated; other levels keep the old path.
- **Details or positions, not both**: a level that isn't stale keeps its positions and no detail; the detail is
  fitted to the positions when a lower level is about to change (before the levels above are marked stale), and
  dropped again once a stale level is evaluated. Folding edits at the current level into the stack no longer
  re-fits every level's detail, which is why Lower Res after sculpting got faster.
- **Coarse topology**: exact capacities (the polygon lists were reserved for all triangles), the quad pairing and
  the UV corner table are derived (a byte per quad says which diagonal it has).
- **Implicit adjacency** (`Mesh::implicitAdjacency`): the working mesh of a generated level (manifold, coarse
  valences up to 32) stores no vertex-vertex or vertex-triangle lists. `Mesh::neighbors` and
  `Mesh::vertexTriangles` return an `IndexRing` (a span-like list, ascending as before) worked out from the
  level's topology: closed forms per kind of fine vertex (corner, edge point, face point; Catmull-Clark and Loop),
  with two small indexes built when the level becomes current (each coarse vertex's polygon corners, each coarse
  edge's sides; about 4.5 bytes per triangle) so most lookups read one or two cache lines and sort at most 4
  values with a branchless network. The lists are identical to the stored ones (tested on quads, mixed
  quads/triangles, open meshes and Loop), so every brush, the normals, undo, the BVH and the renderer give
  bit-identical results. Meshes without levels, non-manifold ones and higher valences keep the stored lists.
- **Project format 6** (core/Project.cpp): generated levels are written without triangles or child maps, and a
  level without a detail takes the one that fits its positions. Projects of format 5 and older open unchanged
  (tested with files saved by 0.5) and their levels become generated. Opening reads each blob straight into its
  array (`readStoredZipIndex` + `pread`, big arrays in parallel) instead of reading the whole archive into memory
  first; the mesh's topology is built once, after its levels are attached; level data is no longer given a full
  topology and normals just to be thrown away; and the BVH of a big mesh is built on its own thread (it only needs
  the positions and triangles) while the levels and topology load, with the main thread's work kept off the
  shared pool meanwhile. Saving writes the mesh arrays straight from memory (`ZipEntry::external`) instead of
  copying them into the archive list first.
- **Divide's limit** (`subdivMaxTriangles`) is what fits in 60% of the computer's memory at 110 bytes per triangle
  (the measured scene plus the peak while dividing), between 50 million and 1 billion: about 160 million on this
  machine.
- **makeCube** welds at every resolution: mirrored sides computed 2i/n - 1 and 2(n-i)/n - 1, which aren't exact
  opposites in floating point, so resolutions such as 13 and 26 (the base of the 33M benchmark sphere) left a few
  unwelded vertices along the cube's edges (reported by the view agent).

### Memory per triangle (33M benchmark sphere)

| | before | after |
|---|---|---|
| Mesh: positions 6, normals 6, triangles 12, quad pairs 4, polygroups 4, normal-update stamps 2, dirty bits | 34 | 34 |
| Mesh: vertex-vertex and vertex-triangle lists (CSR) | 28 | **0** (implicit) |
| Mesh: implicit adjacency indexes (corners per coarse vertex, sides per coarse edge) | | 4.5 |
| Levels: the top level's data (positions 6, triangles 12, quad pairs 4, polygroups 4) | 26 | **10** (positions, polygroups) |
| Levels: the top level's detail | 6 | **0** (fitted to the positions when needed) |
| Levels: the top level's topology (coarse polygons and edges 18 -> 12; child maps 2, parents and corner codes 11 -> 0) | 31 | **12** |
| Levels: everything below the top (a quarter, a sixteenth, ...) | 12 | 7 |
| BVH | 19.5 | 19.5 |
| **Total (CPU)** | **157** | **87** |
| GPU buffers (unchanged; estimate) | about 26 | about 26 |

### Known limits

- The scene is at about 87 bytes per triangle (90 measured as RSS), not the ~60 of the brief: the mesh (39) and
  the levels (29) are down from 137 to 68, but the BVH (19.5, feat/brush's) and the GPU-side buffers are unchanged
  here. What is left in mine: the current level's reference copy of its positions in the stack (6, needed to see
  what a stroke changed), the coarse topology of the top level (12: edges, their polygons, polygon lists), the
  sculpting mesh's triangles (12) and quad pairs (4, which on a generated level are just 2i, 2i + 1), the
  implicit adjacency indexes (4.5) and the normal-update stamps (2). The quad pairs and the coarse edge lists are
  the next candidates.
- Implicit lookups cost a few nanoseconds more per vertex than reading stored lists, so dabs are 0-20% slower
  (most at 4-16M with small brushes; at 33M the large dab is 3.5% slower and CPU-only medium dabs and undo are
  unchanged). The brief asked for no slower dabs: this trades about 23 bytes per triangle (760 MB at 33M) for it.
  Keeping the stored vertex-vertex lists (and only the triangle lists implicit) would win most of that speed back
  for about 10 of those bytes; I tried denser layouts for the implicit lists, which didn't help (the cost is the
  work per lookup, not memory reads).
- Opening a 33M project is bounded by the BVH build (about 1.7 s, running in parallel with the rest, which takes
  about 1.5 s): 2.1-2.5 s instead of 4.7 s, not yet reliably under 2 s. A faster BVH build (feat/brush) or
  storing the BVH in the project would bring it there.
- Format 6 projects don't open in 0.5.

## Benchmark tables (feat/mem)

### Before (main at aac5f68), with the GPU

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 217 |
| First GPU upload | 92 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 719 MB (+648 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.50 | 0.47 | 0.39 | 0.00 | 0.35 | **1.75** | 0.00 | 4.7 | 4.7 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.26 | 3.40 | 1.93 | 0.00 | 2.28 | **11.10** | 0.00 | 9.5 | 9.7 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.34 | 0.33 | 0.23 | 0.00 | 0.28 | **1.22** | 0.00 | 2.2 | 2.6 | 0.9 MB |
| ClayBuildup | 8k | 18k (12) | 0.43 | 0.31 | 0.25 | 0.00 | 0.25 | **1.27** | 0.00 | 3.1 | 2.9 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.24 | 0.18 | 0.12 | 0.00 | 0.17 | **0.74** | 0.00 | 2.4 | 3.2 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.20 | 0.17 | 0.13 | 0.00 | 0.16 | **0.68** | 0.00 | 3.0 | 3.0 | 0.6 MB |
| DamStandard | 4k | 12k (9) | 0.20 | 0.16 | 0.12 | 0.00 | 0.16 | **0.65** | 0.00 | 3.2 | 3.3 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.31 | 0.18 | 0.12 | 0.00 | 0.17 | **0.80** | 0.00 | 4.4 | 4.3 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.10 | 0.09 | 0.00 | 0.13 | **0.37** | 0.00 | 0.3 | 0.2 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.48 | 0.17 | 0.12 | 0.00 | 0.17 | **0.97** | 0.00 | 4.2 | 4.4 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.19 | 0.00 | 0.00 | 0.00 | 0.23 | **0.44** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.18 | 0.00 | 0.00 | 0.00 | 0.30 | **0.50** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 191 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 91 |
| Open project | 573 |
| Orbit: frame time, 4x antialiasing | 5.0 (200 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.2 (314 fps) |

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1183 |
| BVH build | 790 |
| First GPU upload | 358 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2720 MB (+2224 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 2.45 | 2.28 | 1.37 | 0.00 | 1.52 | **7.79** | 0.00 | 14.8 | 12.8 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 8.96 | 9.50 | 5.84 | 0.01 | 8.50 | **33.62** | 0.00 | 34.3 | 32.8 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.20 | 1.20 | 0.78 | 0.01 | 0.78 | **4.05** | 0.00 | 7.7 | 7.8 | 3.5 MB |
| ClayBuildup | 33k | 55k (12) | 1.58 | 1.35 | 0.82 | 0.01 | 0.85 | **4.70** | 0.00 | 8.1 | 8.0 | 3.8 MB |
| Smooth | 19k | 37k (9) | 0.97 | 0.80 | 0.58 | 0.01 | 0.56 | **2.98** | 0.00 | 6.6 | 7.0 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.80 | 0.83 | 0.61 | 0.01 | 0.55 | **2.87** | 0.00 | 7.3 | 7.0 | 2.6 MB |
| DamStandard | 18k | 35k (9) | 0.89 | 0.85 | 0.62 | 0.01 | 0.53 | **2.96** | 0.00 | 6.7 | 6.4 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.22 | 0.91 | 0.63 | 0.01 | 0.57 | **3.41** | 0.00 | 9.6 | 6.8 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.47 | 0.63 | 0.01 | 0.44 | **1.75** | 0.00 | 1.3 | 1.2 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 1.98 | 0.86 | 0.60 | 0.01 | 0.57 | **4.09** | 0.00 | 8.7 | 8.4 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.73 | 0.00 | 0.00 | 0.01 | 0.70 | **1.50** | 0.00 | 0.3 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.66 | 0.00 | 0.00 | 0.01 | 0.96 | **1.70** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 780 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 19 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 345 |
| Open project | 2199 |
| Orbit: frame time, 4x antialiasing | 11.6 (86 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.4 (119 fps) |

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2428 |
| BVH build | 1665 |
| First GPU upload | 736 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5664 MB (+4824 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 149k (21) | 4.05 | 3.26 | 1.98 | 0.00 | 3.07 | **12.72** | 0.00 | 21.8 | 21.2 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 17.52 | 18.05 | 11.48 | 0.01 | 14.05 | **62.60** | 0.00 | 69.1 | 68.4 | 38.7 MB |
| Clay | 58k | 91k (16) | 2.10 | 1.82 | 1.06 | 0.01 | 1.39 | **6.57** | 0.00 | 13.6 | 13.6 | 7.2 MB |
| ClayBuildup | 66k | 102k (17) | 2.61 | 2.11 | 1.14 | 0.01 | 1.72 | **7.82** | 0.00 | 15.5 | 14.8 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.75 | 1.61 | 0.79 | 0.02 | 1.15 | **5.47** | 0.00 | 11.4 | 10.8 | 5.7 MB |
| Inflate | 38k | 66k (14) | 1.63 | 1.53 | 0.80 | 0.02 | 1.29 | **5.43** | 0.00 | 10.8 | 10.3 | 5.6 MB |
| DamStandard | 37k | 65k (15) | 1.62 | 1.61 | 0.80 | 0.02 | 1.24 | **5.44** | 0.00 | 10.4 | 12.2 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.11 | 1.65 | 0.77 | 0.02 | 1.21 | **5.92** | 0.00 | 12.0 | 10.1 | 5.6 MB |
| Move | 33k | 59k (10) | 0.31 | 0.84 | 0.80 | 0.02 | 1.26 | **3.42** | 0.00 | 1.8 | 2.1 | 0.5 MB |
| Recipe (pattern) | 38k | 68k (15) | 2.50 | 1.61 | 0.78 | 0.02 | 1.28 | **6.39** | 0.00 | 11.1 | 10.7 | 5.4 MB |
| Mask | 39k | 67k (15) | 1.41 | 0.00 | 0.00 | 0.02 | 1.65 | **3.22** | 0.00 | 0.8 | 0.6 | 2.7 MB |
| Paint | 39k | 67k (15) | 1.35 | 0.00 | 0.00 | 0.02 | 2.14 | **3.64** | 0.00 | 1.2 | 1.0 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1583 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Save project (1905 MB) | 663 |
| Open project | 4721 |
| Orbit: frame time, 4x antialiasing | 20.9 (48 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.4 (70 fps) |

### After (feat/mem), with the GPU

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 157 |
| BVH build | 247 |
| First GPU upload | 92 |
| Mesh (incl. topology) | 154 MB = 39 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 451 MB (+381 MB = 96 B/triangle) |
| Peak while dividing (RSS) | 491 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.49 | 0.59 | 0.46 | 0.00 | 0.49 | **2.06** | 0.00 | 3.7 | 3.8 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.05 | 3.52 | 1.86 | 0.00 | 3.12 | **11.76** | 0.00 | 11.3 | 10.7 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.31 | 0.35 | 0.24 | 0.00 | 0.32 | **1.24** | 0.00 | 2.4 | 2.3 | 0.9 MB |
| ClayBuildup | 8k | 18k (12) | 0.41 | 0.40 | 0.29 | 0.00 | 0.37 | **1.51** | 0.00 | 3.1 | 2.2 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.26 | 0.23 | 0.15 | 0.00 | 0.24 | **0.89** | 0.00 | 2.2 | 2.1 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.19 | 0.21 | 0.15 | 0.00 | 0.23 | **0.81** | 0.00 | 2.8 | 2.0 | 0.6 MB |
| DamStandard | 4k | 12k (9) | 0.20 | 0.21 | 0.14 | 0.00 | 0.23 | **0.79** | 0.00 | 2.1 | 2.0 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.28 | 0.22 | 0.14 | 0.00 | 0.24 | **0.90** | 0.00 | 2.0 | 1.9 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.13 | 0.11 | 0.00 | 0.20 | **0.47** | 0.00 | 0.3 | 0.3 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.47 | 0.22 | 0.14 | 0.00 | 0.24 | **1.09** | 0.00 | 1.9 | 2.0 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | 0.28 | **0.48** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | 0.35 | **0.54** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 110 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 2 |
| Save project (165 MB) | 54 |
| Open project | 298 |
| Open project: memory (RSS) | +414 MB, peak +457 MB |
| Orbit: frame time, 4x antialiasing | 4.8 (209 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.0 (337 fps) |

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 797 |
| BVH build | 1098 |
| First GPU upload | 360 |
| Mesh (incl. topology) | 614 MB = 39 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1640 MB (+1272 MB = 80 B/triangle) |
| Peak while dividing (RSS) | 1599 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 2.30 | 2.46 | 1.56 | 0.00 | 1.95 | **8.46** | 0.00 | 16.6 | 10.8 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 9.02 | 9.63 | 5.99 | 0.01 | 10.29 | **35.68** | 0.00 | 35.2 | 34.5 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.18 | 1.44 | 0.93 | 0.01 | 1.03 | **4.68** | 0.00 | 8.7 | 8.6 | 3.5 MB |
| ClayBuildup | 33k | 55k (12) | 1.75 | 1.77 | 1.18 | 0.01 | 1.28 | **6.09** | 0.00 | 11.7 | 9.5 | 3.8 MB |
| Smooth | 19k | 37k (9) | 1.05 | 0.94 | 0.65 | 0.01 | 0.77 | **3.50** | 0.00 | 6.5 | 6.6 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.83 | 1.07 | 0.79 | 0.01 | 0.76 | **3.52** | 0.00 | 9.3 | 7.2 | 2.6 MB |
| DamStandard | 18k | 35k (9) | 0.86 | 0.96 | 0.67 | 0.01 | 0.73 | **3.30** | 0.00 | 6.6 | 6.5 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.22 | 1.03 | 0.72 | 0.01 | 0.77 | **3.80** | 0.00 | 8.2 | 6.9 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.60 | 0.74 | 0.01 | 0.60 | **2.16** | 0.00 | 1.7 | 1.7 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 2.07 | 1.02 | 0.67 | 0.01 | 0.80 | **4.63** | 0.00 | 8.0 | 7.9 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.72 | 0.00 | 0.00 | 0.01 | 0.91 | **1.71** | 0.00 | 0.3 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.64 | 0.00 | 0.00 | 0.01 | 1.20 | **1.92** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 434 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 24 |
| Higher Res again (no edits) | 6 |
| Save project (658 MB) | 231 |
| Open project | 1047 |
| Open project: memory (RSS) | +1621 MB, peak +1875 MB |
| Orbit: frame time, 4x antialiasing | 11.9 (84 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.4 (118 fps) |

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1415 |
| BVH build | 1675 |
| First GPU upload | 710 |
| Mesh (incl. topology) | 1281 MB = 39 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3306 MB (+2520 MB = 76 B/triangle) |
| Peak while dividing (RSS) | 3185 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 147k (22) | 4.19 | 3.47 | 1.94 | 0.00 | 3.77 | **13.71** | 0.00 | 25.5 | 23.4 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 17.54 | 19.03 | 11.59 | 0.01 | 15.89 | **65.42** | 0.00 | 71.5 | 69.9 | 38.7 MB |
| Clay | 58k | 90k (16) | 2.09 | 1.82 | 1.03 | 0.01 | 1.85 | **7.02** | 0.00 | 14.3 | 14.1 | 7.2 MB |
| ClayBuildup | 66k | 101k (17) | 2.48 | 2.17 | 1.25 | 0.01 | 2.06 | **8.19** | 0.00 | 15.3 | 15.2 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.71 | 1.58 | 0.80 | 0.02 | 1.51 | **5.77** | 0.00 | 11.0 | 10.8 | 5.7 MB |
| Inflate | 38k | 65k (14) | 1.54 | 1.80 | 0.95 | 0.02 | 1.46 | **5.94** | 0.00 | 10.7 | 11.0 | 5.6 MB |
| DamStandard | 37k | 64k (14) | 1.54 | 1.76 | 0.91 | 0.02 | 1.64 | **6.03** | 0.00 | 10.4 | 10.4 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.14 | 1.66 | 0.82 | 0.02 | 1.43 | **6.22** | 0.00 | 11.3 | 11.2 | 5.6 MB |
| Move | 33k | 58k (11) | 0.27 | 0.88 | 0.76 | 0.02 | 1.22 | **3.32** | 0.00 | 1.5 | 2.0 | 0.5 MB |
| Recipe (pattern) | 38k | 67k (14) | 2.66 | 1.78 | 0.81 | 0.02 | 1.58 | **7.03** | 0.00 | 11.4 | 11.4 | 5.4 MB |
| Mask | 39k | 66k (15) | 1.48 | 0.00 | 0.00 | 0.02 | 1.84 | **3.48** | 0.00 | 0.8 | 0.6 | 2.7 MB |
| Paint | 39k | 66k (15) | 1.38 | 0.00 | 0.00 | 0.02 | 2.30 | **3.83** | 0.00 | 1.2 | 1.0 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 876 |
| Higher Res (back up) | 13 |
| Lower Res again (no edits) | 55 |
| Higher Res again (no edits) | 11 |
| Save project (1373 MB) | 433 |
| Open project | 2137 |
| Open project: memory (RSS) | +3475 MB, peak +4017 MB |
| Orbit: frame time, 4x antialiasing | 20.0 (50 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.6 (68 fps) |

### After, CPU only, 64M

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2809 |
| BVH build | 3347 |
| Mesh (incl. topology) | 2456 MB = 39 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1879 MB = 29 B/triangle |
| Process memory (RSS) | 5763 MB (+5753 MB = 90 B/triangle) |
| Peak while dividing (RSS) | 6786 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 198k | 259k (28) | 6.58 | 4.76 | 2.82 | 0.00 | **14.68** | 0.00 | 43.2 | 43.1 | 21.8 MB |
| Standard (large, r 0.4) | 1.9M | 2.0M (79) | 33.33 | 36.58 | 22.48 | 0.01 | **94.81** | 0.00 | 138.0 | 135.5 | 74.0 MB |
| Clay | 110k | 155k (21) | 4.23 | 2.97 | 1.75 | 0.02 | **9.31** | 0.00 | 25.6 | 25.8 | 13.8 MB |
| ClayBuildup | 120k | 166k (23) | 4.38 | 3.07 | 1.75 | 0.02 | **9.58** | 0.00 | 29.2 | 28.1 | 15.1 MB |
| Smooth | 71k | 108k (19) | 3.11 | 2.26 | 1.12 | 0.03 | **6.76** | 0.00 | 20.9 | 21.3 | 10.8 MB |
| Inflate | 69k | 106k (19) | 2.81 | 2.30 | 1.25 | 0.03 | **6.65** | 0.00 | 20.6 | 20.2 | 10.7 MB |
| DamStandard | 68k | 104k (18) | 2.59 | 2.19 | 1.15 | 0.03 | **6.21** | 0.00 | 19.6 | 19.4 | 10.4 MB |
| Layer | 70k | 107k (19) | 3.73 | 2.34 | 1.18 | 0.03 | **7.55** | 0.00 | 20.7 | 21.6 | 10.8 MB |
| Move | 64k | 98k (13) | 0.55 | 1.12 | 1.14 | 0.03 | **3.17** | 0.00 | 2.5 | 2.9 | 1.0 MB |
| Recipe (pattern) | 72k | 109k (19) | 4.01 | 2.42 | 1.15 | 0.04 | **7.90** | 0.00 | 21.0 | 21.2 | 10.6 MB |
| Mask | 72k | 108k (19) | 2.46 | 0.00 | 0.00 | 0.03 | **2.71** | 0.00 | 1.6 | 1.3 | 5.3 MB |
| Paint | 72k | 108k (19) | 2.34 | 0.00 | 0.00 | 0.03 | **2.57** | 0.00 | 2.6 | 2.3 | 10.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 1551 |
| Higher Res (back up) | 25 |
| Lower Res again (no edits) | 76 |
| Higher Res again (no edits) | 21 |
| Save project (2633 MB) | 846 |
| Open project | 6307 |
| Open project: memory (RSS) | +6822 MB, peak +7821 MB |

### After, CPU only, 104M (no project round trip)

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 104.0M triangles (52.0M vertices, 8 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4889 |
| BVH build | 5896 |
| Mesh (incl. topology) | 4011 MB = 39 B/triangle |
| BVH | 2043 MB = 19.6 B/triangle |
| Subdivision levels (stack) | 3068 MB = 29 B/triangle |
| Process memory (RSS) | 8974 MB (+8964 MB = 86 B/triangle) |
| Peak while dividing (RSS) | 10982 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 324k | 407k (43) | 9.69 | 7.10 | 5.00 | 0.00 | **22.61** | 0.00 | 70.3 | 68.2 | 35.6 MB |
| Standard (large, r 0.4) | 3.1M | 3.2M (114) | 54.36 | 60.70 | 42.49 | 0.02 | **161.53** | 0.00 | 262.4 | 261.4 | 121.1 MB |
| Clay | 181k | 242k (32) | 6.10 | 4.26 | 2.59 | 0.03 | **13.52** | 0.00 | 47.7 | 51.7 | 22.6 MB |
| ClayBuildup | 193k | 256k (34) | 6.94 | 4.91 | 3.10 | 0.04 | **15.59** | 0.00 | 51.3 | 52.3 | 24.4 MB |
| Smooth | 115k | 164k (28) | 4.76 | 3.13 | 1.60 | 0.05 | **9.96** | 0.00 | 35.2 | 35.1 | 17.4 MB |
| Inflate | 113k | 161k (27) | 4.43 | 3.27 | 1.85 | 0.05 | **10.01** | 0.00 | 34.7 | 36.1 | 17.2 MB |
| DamStandard | 111k | 159k (28) | 4.08 | 3.26 | 1.67 | 0.05 | **9.44** | 0.00 | 37.3 | 39.0 | 16.7 MB |
| Layer | 114k | 163k (28) | 5.86 | 3.11 | 1.69 | 0.05 | **11.11** | 0.00 | 41.4 | 38.9 | 17.4 MB |
| Move | 105k | 152k (30) | 0.78 | 1.57 | 1.83 | 0.05 | **4.76** | 0.00 | 4.1 | 4.1 | 1.7 MB |
| Recipe (pattern) | 117k | 167k (28) | 6.53 | 3.10 | 1.49 | 0.05 | **11.58** | 0.00 | 37.2 | 36.8 | 17.2 MB |
| Mask | 118k | 166k (28) | 3.85 | 0.00 | 0.00 | 0.05 | **4.26** | 0.00 | 2.6 | 2.3 | 8.6 MB |
| Paint | 118k | 166k (28) | 3.95 | 0.00 | 0.00 | 0.05 | **4.36** | 0.00 | 4.2 | 4.0 | 17.3 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 26.0M, after edits) | 2776 |
| Higher Res (back up) | 32 |
| Lower Res again (no edits) | 133 |
| Higher Res again (no edits) | 32 |

