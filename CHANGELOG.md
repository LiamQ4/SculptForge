# Changelog

All notable changes to SculptForge are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### User guide
- A complete user guide (21 chapters, from the interface and every brush to Send to Unreal, scripting and
  troubleshooting, with screenshots) built into the app: Help > User Guide (Shift+F1), with contents, full-text
  search, links, back / forward and copyable code. Hold Ctrl over a control and press F1 to open its chapter.
- The same guide on the website (guide/), generated from docs/guide by tools/guide/build_site.py, with search,
  a contents drawer on phones and swipe / arrow keys between chapters.
- Website motion: page transitions (View Transitions), scroll-linked effects, a progress bar and staggered
  reveals tuned for phones, tablets and monitors; all off with "reduce motion".

### A ZBrush-style interface
- Laptop camera notches: when SculptForge fills the screen, the interface stays below the camera notch of
  MacBooks (macOS reports the safe area; on Linux a notched MacBook display is recognised), with a plain band
  beside the notch. Preferences > Interface > Camera notch: Automatic / Always reserve / Off.
- Fullscreen toggle (F11, Render palette; `--fullscreen` on the command line), and minimise / close buttons at
  the far right of the title strip (close asks to save changes; Ctrl+Q quits).
- The canvas is framed by shelves, as in ZBrush: a top shelf (LightBox, Sculpt/Paint, Zadd/Zsub, Draw Size,
  Strength, symmetry, pressure), a left shelf of Brush / Stroke / Alpha / Material pickers with the colour picker,
  a right shelf of navigation (drag Move / Zoom / Rotate) and display toggles, and a bottom shelf of your most-used
  brushes. 16 alphabetical palettes hold every tool; the Tool palette (SubTool, Layers, Geometry, Remesh,
  Masking, Polygroups, Visibility, Export, Display) sits in the right tray, and any palette can move into a tray.
- LightBox (`,`): projects, models, brushes, alphas, materials, images, splats, QuickSaves and recent files as
  thumbnails across the top of the canvas; brush thumbnails are sculpted for real; projects save a thumbnail.
- Pop-up pickers: Brush (`B`, then type letters: B S T = Standard), Material (`M`, previews on the model as you
  hover), Draw Size / Strength at the cursor (`S` / `I`).
- Hold Ctrl over any button for what it does, its shortcut and its command (Ctrl+Alt for scripting parameters).
- Command palette (F3 / Ctrl+Shift+P) finds and runs anything; the console (`` ` ``) shows every command as a
  script line, with completion and history, and copies the session as a macro.
- Customize: Preferences > Config > Enable Customize, then Ctrl+Alt+drag to add, move or remove shelf items;
  Store Config (Ctrl+Shift+I), Restore Standard UI, Save / Load UI, three layout presets, and reassignable
  shortcuts (Ctrl+Alt+click a button, then press a key). Tab hides the shelves.
- Lucide icons (ISC/MIT). Brush Lab now opens from the "AI Brush" button instead of covering the model at start.

### Graphics tablets
- Preferences > Tablet: the pen in use, a live pressure meter, a draw-test area that shows the pressure along
  your stroke, a global pressure curve (Soft / Linear / Firm, curve, min / max, threshold, full-pressure point)
  applied before each brush's own curve, Pressure > Strength / Size, and the pen's tilt when reported.
- The eraser end inverts the brush (Ctrl + eraser erases mask); it can be set to Smooth, Erase mask or Nothing.
- Pen side buttons are mapped by SculptForge instead of SDL's version-dependent clicks: lower = right-click
  (drag to rotate, context menus), upper = Alt (invert), third = pan; each can be Invert, Smooth, Rotate view,
  Pan view, Right-click or Nothing.
- No pressure spike at the start of a stroke (the first dab waits for the touch's own pressure sample), every
  pen sample is used with the pressure interpolated between samples, the mouse always presses fully, and a pen
  that leaves the tablet's range mid-stroke ends the stroke cleanly.
- UI tests drive a simulated pen (`pen_stroke`, `pen_button`, `pen_hover`, ...), see tests/ui/tablet.sfui.
  Guide: docs/tablet.md.

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

### Brush library and sharing
- Custom brushes persist: Brush Lab saves every brush made or refined there to your brush library (a moment
  after the last change), and it loads at every start in the app, `sculptforge-cli` and Python.
- Brush codes: right-click a brush > Copy brush code gives one line of text (`sfbrush1:...`) to share in chat
  or a forum. Ctrl+V in the viewport, or Paste brush in Brush Lab, installs a pasted code or `.sfbrush` text.
  Dropping or opening a `.sfbrush` file installs it too.
- The Custom brushes list shows where each brush comes from (studio, gallery, project, unsaved, or `yours` when
  your version replaces a built-in or studio brush) and has a right-click menu: copy code, save, rename,
  delete (with confirmation), reveal in folder.
- Studio brush folders: list read-only shared folders in `SCULPTFORGE_BRUSH_PATH`; the Rez package adds
  `SCULPTFORGE_STUDIO_BRUSHES` when set. Precedence: yours > studio > built-in.
- New commands `brush_install`, `brush_code`, `brush_delete`, `brush_rename` and `brush_library`;
  `brush_list` shows each brush's source.
- Community brushes (View > Community brushes): browse, search and install from the CC0 gallery at
  github.com/islandmed/sculptforge-brushes (21 brushes to start), with checksums, an offline cache and a
  "Share yours" link; also on the website (brushes.html) and as `brush_gallery_search` / `brush_gallery_install`.
- New `stroke` command sculpts a stroke along points headlessly (scripts, MCP agents, brush previews).

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
