# SculptForge Roadmap

## Why SculptForge? (positioning)

| | ZBrush | Blender (sculpt mode) | Nomad Sculpt | SculptGL | **SculptForge** |
|---|---|---|---|---|---|
| Price / license | Paid subscription, closed | Free, GPL | Paid, closed | Free, MIT | **Free, GPL** |
| Linux | No | Yes | No | Browser | **Yes (native)** |
| Focus | Sculpting | General 3D suite | Sculpting (mobile-first) | Demo | **Desktop sculpting only** |
| Very high poly counts | Excellent | Good | Good | Limited | Goal: excellent |
| ZBrush-style controls | — | Partial (keymap) | Partial | No | **Yes, by default** |
| Headless / scriptable core | Limited (ZScript) | Python (whole app) | No | No | **Yes (core library + commands)** |
| AI assistant | Limited | Add-ons only | No | No | **Bring your own AI: brush design today** |
| Image to 3D | No | Add-ons only | No | No | **Free Hunyuan3D / TRELLIS.2 / SF3D via Hugging Face, paid fal.ai / Meshy, local generators** |
| Gaussian splats | No | Add-ons only | No | No | **View captures, splat ↔ mesh, PLY / .splat / glTF / USD** |
| OpenUSD | Export plugin | Yes | No | No | **USDA/USDZ with UsdSkel blend shapes and splat prims** |
| Unreal Engine | FBX export / GoZ-style bridges | FBX export | No | No | **Send to Unreal (live, no plugin), LODs + morph targets in one FBX** |
| Agent control (MCP) | No | Add-ons only | No | No | **Built in: `sculptforge-cli --mcp`** |
| Blend shapes / LODs / GLB | Via export plugins | Yes (general 3D) | Partial | No | **Built in, one click** |
| Open file formats | Proprietary .ZTL | .blend | Proprietary | OBJ | **OBJ, FBX, glTF, PLY, USD/USDZ** |

Where SculptForge aims to win:

1. **Focus and speed** — a sculpt-only tool that starts instantly and stays responsive at tens of millions of polygons.
2. **First-class Linux** alongside macOS and Windows.
3. **Familiar to ZBrush users** — controls work the way they expect.
4. **Scriptable** — every action is a named command callable from UI, scripts or AI.
5. **Bring your own AI** — opt-in, provider-agnostic, works fully offline with local models.

Where we deliberately do *not* compete yet: ZRemesher-class automatic
retopology, a full ZBrush feature list, rendering/animation (use Blender).

## Status key

✅ done · 🚧 in progress · 📋 planned

## Phase 1 — Core sculpting (v0.1)

- ✅ Cross-platform app shell (SDL3, OpenGL 3.3, Dear ImGui)
- ✅ BVH-accelerated picking and brush queries with incremental refit
- ✅ Brushes: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch
- ✅ Symmetry (X/Y/Z), pen pressure, stroke spacing
- ✅ Undo/redo (sparse per-stroke + full snapshots for topology changes)
- ✅ Loop subdivision, sphere/plane primitives
- ✅ OBJ import/export, native file dialogs, drag & drop
- ✅ Command registry with LLM tool-schema export
- 🚧 CI builds and tests on Linux, macOS, Windows (workflow written; verified locally on Linux only)

## Phase 1.5 — Studio pipeline & AI (done)

- ✅ Sculpt layers exported as glTF morph targets (blend shapes), expression preview
- ✅ Quadric LOD chain with preview; layers preserved on every LOD
- ✅ glTF/GLB export + import (seam welding, morph targets → layers)
- ✅ `sculptforge-cli` headless commands, `$variables`, LLM tool-schema export
- ✅ Headless thumbnail/turntable rendering
- ✅ Brush Lab: recipe brush engine, procedural patterns, `.sfbrush` files, AI brush design
  (Anthropic or any OpenAI-compatible/local model)
- ✅ Image to 3D: sketch inflation, relief, view projection, pluggable generator command

## Phase 1.6 — Open standards & capture (done)

- ✅ AI image-to-3D: free via Hugging Face Spaces (built-in Gradio client: Hunyuan3D 2.1 / 2 / Turbo,
  TRELLIS.2, Stable Fast 3D), or paid via fal.ai / Meshy; subject framing; auto voxel remesh on import
- ✅ Voxel remesh (DynaMesh-style): watertight, even surface; merges parts; fixes scans
- ✅ OpenUSD: USDA/USDZ export (UsdSkel blend shapes, UsdPreviewSurface, splats as
  `ParticleField3DGaussianSplat`), USDA/USDZ import; validated with OpenUSD 26.08 `usdchecker`
- ✅ Gaussian splats: PLY (INRIA layout, SH up to degree 3), `.splat`, glTF `KHR_gaussian_splatting`,
  USD ParticleField; sorted EWA splat rendering; splats → watertight mesh; sculpt → splats
- ✅ PLY mesh / point cloud import
- ✅ Unreal Engine: binary FBX 7.4 export (LOD groups, blend shapes), FBX import (ufbx), Unreal preset,
  Send to Unreal over Python remote execution (same machine or LAN)
- ✅ MCP server (`sculptforge-cli --mcp`): every command as a tool for Claude Code / Claude Desktop / any MCP client

## Phase 2 — Detail & workflow (v0.2–0.3)

- 📋 Masking (paint, invert, blur, mask-by-cavity) with brushes respecting masks
- 📋 Multiresolution: subdivision levels you can step up/down while keeping detail
- 📋 Dynamic topology (local remeshing under the brush)
- 📋 Alphas / stencils and stroke types (drag-rect, spray, lazy mouse)
- 📋 More brushes: Clay Buildup, Dam Standard, Snake Hook, Trim, Layer, Nudge
- 📋 MatCap materials, cavity/AO shading
- 📋 Quit-with-unsaved-changes prompt, recent files, project file format
- 📋 Multithreaded brush evaluation, partial GPU uploads per BVH node
- 📋 Benchmarks: brush latency at 1M / 5M / 20M triangles in CI

## Phase 3 — AI assistant, beyond brushes (v0.4)

- 📋 Chat assistant that drives any SculptForge command (tool calling over the existing registry)
- 📋 OS keychain storage for API keys
- 📋 Preview/confirm for destructive AI actions
- 📋 Viewport screenshot as context (proportion/anatomy feedback)
- 📋 Text-to-3D through the generator interface
- 📋 Multi-view image-to-3D (front/side/back) and image-conditioned retopology
- 📋 Viewport snapshot tool for MCP agents (let the agent see what it sculpted)
- 📋 Normal/displacement map baking (high → low poly), auto-retopology + UVs
- 📋 Live link to Unity / Godot / Blender (Unreal done); Unreal: send textures/normal maps, Nanite option

## Phase 4 — Advanced modeling (v0.5+)

- 📋 Boolean merge/subtract on the voxel pipeline; sharper voxel remesh (signed distance instead of occupancy)
- 📋 Retopology tools and quad remeshing
- 📋 Multiple subtools/objects, layers
- 📋 Vertex painting (polypaint), UVs, texture baking (normal/displacement)
- 📋 STL, binary USD (usdc), SPZ-compressed splats, USD references/payloads
- 📋 Splat editing: crop box, paint-select, sculpt deforming nearby splats

## Phase 5 — Ecosystem (v1.0)

- 📋 Python scripting & plugin API built on the command registry
- 📋 Installers: AppImage/Flatpak, signed macOS .dmg, Windows MSI
- 📋 Documentation site and tutorials
