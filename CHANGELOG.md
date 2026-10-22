# Changelog

All notable changes to SculptForge are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### ComfyUI
- Generate 3D can run on your own ComfyUI (local or a studio GPU server): any API-format workflow, with
  ready-made Hunyuan3D 2.1 image-to-3D and FLUX + Hunyuan3D text-to-3D workflows, a connection test, queue
  progress, cancel, and clear errors (missing nodes or models, UI-format files). `image_to_3d` / `text_to_3d`
  take `service=comfyui workflow= url=`; new `comfyui_check`. See docs/comfyui.md.
- ComfyUI-SculptForge node pack (integrations/comfyui): load, inflate image, relief, voxel remesh, decimate,
  subdivide, LOD chain, export (GLB, FBX, USDA/USDZ, OBJ, STL, PLY), mesh info and open in SculptForge; works
  with ComfyUI's core MESH type.
- tools/comfyui-validate-workflow.py and tools/comfyui-smoke.sh check and run workflows against a server.

### SubTools
- A scene can hold several meshes (SubTools), like ZBrush: each keeps its own attributes, sculpt layers and undo
  history. Only the active one is sculpted; the others are drawn slightly dimmed.
- SubTools panel: select, rename (double-click), show/hide, Solo, Unhide all, reorder, Append (sphere, cube,
  plane or a model file), Duplicate, Delete, Merge down, Merge visible, Split by polygroups, Split into parts.
  Alt+click a SubTool in the viewport to select it. New `subtool_*` commands for scripts, Python and MCP agents.
- Files: projects save every SubTool (format version 2; version 1 projects still open). OBJ (`o` objects), glTF,
  FBX (an LOD group per SubTool), USD and GoZ export each visible SubTool as its own object, and multi-object
  files open as SubTools. PLY, STL and splat conversion use the visible SubTools merged.
- OBJ import: `g` groups now become polygroups (as ZBrush writes them) and `o` objects become SubTools.
- New cube primitive.

### Fixed
- Voxel remesh no longer leaves concentric voxel rings on curved surfaces and lands exactly on the original
  surface (it meshes the signed distance instead of blurred voxels).
- Side panels (Brush Lab, Tools, Generate 3D...) never grow past the bottom of the window. They move up
  and scroll instead, so expanded sections such as AI provider are always reachable.

### Development
- UI tests: `sculptforge --ui-test script.sfui` drives the real interface (clicks, typing, strokes,
  shortcuts), checks what it shows and saves screenshots. They run in CI on Linux.

### AI
- Brush Lab is free out of the box: the new default **Free** provider uses an open model hosted by
  Pollinations.ai with no account or key, and falls back to a running local Ollama. Key-less Anthropic
  settings from earlier versions switch to Free automatically.
- Local models made easy: Ollama and LM Studio presets, **Find models** (checks the connection and lists
  installed models), and setup steps for your OS in the AI provider panel. New guide:
  [Run AI on your own computer](docs/local-ai.md). The default local model is now `qwen3:4b-instruct`.
- Switching the Brush Lab provider no longer resets your image-to-3D settings.

## [0.2.0] - 2026-10-21

### Sculpting
- New brushes: Dam Standard, Clay Buildup, Snake Hook, Trim (dynamic; Alt fills), Layer and Nudge. Keys 1-9 and 0
  select the first ten brushes.
- Stroke types: dots, drag-rect, spray and lazy mouse.
- Brush alphas: round, square, noise or any grayscale image (`brush_set_alpha`).
- Mesh attributes shared by every tool: mask, polypaint, polygroups, hidden faces and UVs.

### Masking
- Hold Ctrl while stroking to paint the mask, Ctrl+Alt to erase it. Ctrl+click the background inverts it and
  Ctrl+drag there clears it. Every brush and polypaint leave masked areas alone.
- Commands `mask_clear`, `mask_all`, `mask_invert`, `mask_blur`, `mask_sharpen`, `mask_grow`, `mask_shrink`,
  `mask_by_cavity` and `mask_by_polygroup`, plus a Masking section in the Tools panel.

### Polygroups and visibility
- `polygroup_auto_by_normals`, `polygroup_by_connectivity`, `polygroup_from_mask`, `polygroup_clear`,
  `polygroup_merge_visible`.
- ZBrush-style visibility: Ctrl+Shift+click isolates a polygroup (click again to invert), Ctrl+Shift+Alt+click
  hides it, Ctrl+Shift+click on the background shows everything, Ctrl+Shift+drag hides outside a rectangle (Alt:
  inside). Commands `hide_polygroup`, `isolate_polygroup`, `show_all`, `invert_visibility`, `hide_rect` and
  `delete_hidden`. Brushes, polypaint and mask operations skip hidden faces.

### Polypaint and materials
- Paint brush (P), "RGB while sculpting", colour pick (C), and the `set_color`, `paint_fill`, `paint_clear` and
  `paint_from_cavity` commands. Fills respect the mask by default.
- Six built-in MatCap materials (Red Wax, Clay, Skin, Chrome, White, Bronze) in a new Polypaint & Materials panel.
- Vertex colours are exported to PLY (`export_ply`), USD (`displayColor`), glTF, OBJ and GoZ.

### Projects
- Native `.sfproj` project files (`save_project`, `open_project`, and `open`) hold the mesh, sculpt layers, mask,
  polypaint, polygroups, visibility, UVs, splats, the brush library and view settings.
- Ctrl+S / Ctrl+Shift+S save a project; OBJ export moved to File > Export OBJ.
- Save / Don't Save / Cancel prompt before losing unsaved changes, unsaved marker in the title bar, recent files,
  and autosave with an offer to restore after a crash.

### Interchange
- ZBrush GoZ: `.GoZ` import and export with polygroups, mask, polypaint and UVs (quads are kept), and the GoZ
  bridge (`goz_send`, `goz_receive`, File > ZBrush (GoZ)) on Windows and macOS. The format follows the open-source
  GoB add-on; the live bridge has not yet been tested against a running ZBrush. See [docs/goz.md](docs/goz.md).
- STL import (binary and ASCII) and binary STL export. Opening a `.ZTL`/`.ZPR` file now explains that the format is
  proprietary and suggests GoZ or OBJ/FBX.

### Security and robustness
- File loaders were fuzzed with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer (about 8 million
  inputs). Malformed PLY headers could request huge allocations or cast invalid values, and NaN or far-off
  coordinates could overflow the vertex-welding grid; all are fixed, with regression tests.
- The GoZ bridge refuses shell-unsafe paths before launching ZBrush.

## [0.1.0] - 2026-10-20

First public release.

### Sculpting
- Brushes: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch; X/Y/Z symmetry; tablet pressure;
  ZBrush-style modifiers and navigation.
- BVH-accelerated picking with incremental refit; memory-budgeted unlimited undo.
- Loop subdivision, voxel remesh (DynaMesh-style), quadric decimation.
- Sculpt layers (blend shapes) with live weights and playback.

### AI (free built in, or bring your own)
- Brush Lab: describe a brush, get a procedural brush recipe; Anthropic or any OpenAI-compatible endpoint,
  including local models through Ollama / LM Studio.
- Generate 3D from an image or from text: free via Hugging Face Spaces (Hunyuan3D, TRELLIS.2, Stable Fast 3D),
  or paid via fal.ai / Meshy, or your own generator command; results are remeshed for sculpting.
- Offline image tools: silhouette blockout, relief, stamp-from-view.

### Pipelines and formats
- FBX export (binary 7.4, LOD groups, blend shapes) and import; glTF/GLB (morph targets); OpenUSD USDA/USDZ
  (UsdSkel blend shapes, UsdPreviewSurface); OBJ; PLY.
- Gaussian splats: PLY, .splat, glTF `KHR_gaussian_splatting`, OpenUSD `ParticleField3DGaussianSplat`; splat
  rendering; splats to mesh; sculpt to splats.
- Game-ready presets: one-click LOD chains; Unreal Engine preset and **Send to Unreal** over Python remote
  execution (same machine or local network).
- `sculptforge-cli` for scripting and pipelines, with an MCP server (`--mcp`) for AI agents.

### Studios
- VFX Reference Platform CY2026 / CY2027 builds (Rocky Linux 8/9, GCC 14, C++20), verified in CI.
- Python 3.13 module (`import sculptforge`): every command, NumPy mesh access, layers, scripts.
- Rez package (`packaging/rez`).
- Headless thumbnail/turntable rendering and scripted demo recording.

[0.2.0]: https://github.com/islandmed/SculptForge/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/islandmed/SculptForge/releases/tag/v0.1.0
