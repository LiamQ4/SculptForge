# SculptForge project files (.sfproj)

A `.sfproj` is an uncompressed ("stored") ZIP (src/io/Zip.cpp) written by `saveProject` /
read by `loadProject` (src/core/Project.cpp). Commands: `save_project path=...`,
`open_project path=...`; `open` also recognises `.sfproj`.

## Layout (version 2)
- `manifest.json`: always first. `format` = `"SculptForge project"`, `version` (integer, currently 2),
  `app_version`, then `subtools` (one entry per SubTool, in list order), `active_subtool` (index),
  `brushes` (recipe JSON), `active_brush` (name), optional `splats`, and app-owned keys (`camera`,
  `render`, `brush`, `symmetry`).
- Each `subtools[i]` holds `name`, `visible` (bool), `mesh`, `layers`, `active_layer` (sculpt layer
  strokes go into, -1 = base) and, for a mesh that came from ZBrush, `goz_name` (the GoZ object name
  that `goz_send` / `goz_receive` match on).
- Blobs are little-endian arrays. The manifest describes each one as
  `{"file", "type": "f32"|"u32"|"u8", "components", "count"}`; readers check all four. SubTool `i`'s
  blobs live under `subtools/<i>/` (the file name comes from the descriptor, so readers don't rely
  on it).

| Manifest key (in `subtools[i]`) | File | Layout |
|--------------|------|--------|
| `mesh.positions` | subtools/i/mesh/positions.bin | f32 x3 per vertex (current blend) |
| `mesh.triangles` | subtools/i/mesh/triangles.bin | u32 x3 per triangle |
| `mesh.base` | subtools/i/mesh/base.bin | f32 x3 per vertex (only with layers) |
| `mesh.mask` / `colors` | subtools/i/mesh/mask.bin, colors.bin | f32 x1 / x3 per vertex (linear RGB) |
| `mesh.polygroups` / `hidden` | subtools/i/mesh/polygroups.bin, hidden.bin | u32 / u8 per triangle |
| `mesh.uvs` | subtools/i/mesh/uvs.bin | f32 x2 per triangle corner |
| `layers[k].delta` | subtools/i/layers/<k>.bin | f32 x3 per vertex, plus `name`, `weight` |
| `paint_layers.base` | subtools/i/paint/base.bin | f32 x3 per vertex: the Base paint layer (linear RGB) |
| `paint_layers.layers[k].color` / `alpha` | subtools/i/paint/<k>_color.bin, <k>_alpha.bin | f32 x3 / x1 per vertex, plus `name`, `opacity` (0-1), `blend`, `visible`, `locked` |

| Manifest key (top level) | File | Layout |
|--------------|------|--------|
| `splats.*` | splats/*.bin | positions x3, rotations x4 (x,y,z,w), scales x3, opacities x1, sh x3 per coefficient; plus `count`, `sh_degree`, `name`, `transform` (16 floats, column-major) |

Optional attributes are simply absent when the mesh doesn't have them.

### Paint layers
`subtools[i].paint_layers` (only written when the SubTool has paint layers; src/core/PaintLayer.h) holds
`active` (the layer painting writes into, -1 = Base), `base` and `layers` (bottom to top). `blend` is one of
`normal`, `multiply`, `screen`, `overlay`, `soft_light`, `add`, `subtract`, `darken`, `lighten`, `difference`
(unknown modes load as `normal`). `mesh.colors` still holds the composited stack, so readers that don't know
`paint_layers` (SculptForge 0.3 and earlier) open the project with the paint flattened, and the key needs no
version bump.

Loading replaces the scene: every SubTool comes back with its name, visibility, attributes, layers,
active layer and GoZ name, the saved active SubTool is selected, every SubTool starts with an empty
undo history and the document is clean. A file is refused (and the open scene left alone) when the
SubTool list is missing or empty, or any SubTool's mesh is broken; an out-of-range `active_subtool`
selects the first one.

## Version 1
Version 1 files (SculptForge 0.2 and earlier) have one mesh at the top level instead of `subtools`:
`mesh` and `layers` as above (blobs under `mesh/` and `layers/`) and `active_layer`. They still load,
as a single SubTool called "Sculpt". Saving always writes version 2.

## Compatibility rules
- Unknown keys and files are ignored. Unknown **top-level** keys are kept in
  `Document::projectSettings` and written back, so older builds don't drop newer settings.
- A file whose `version` is greater than `kProjectVersion` is refused with a message naming the app
  version that wrote it. Bump `kProjectVersion` only for changes old readers would misread;
  adding a new optional blob does not need a bump.
- New mesh attributes: add a blob in `writeMesh` and read it in `readMesh` (size-checked); both
  are used for every SubTool.

## App behaviour (src/app/PanelProject.cpp)
- `Document::dirty()` = any undo push/undo/redo (in any SubTool), splat edit or `modified` since
  `markClean()` (save, open, new). The window title shows `*` while dirty.
- New / Open / Open Recent / drop / Quit / window close ask Save / Don't Save / Cancel when dirty
  (never in `--render`, `--record` or demos).
- The pref dir (`SDL_GetPrefPath`) holds `project.json` (10 recent files, autosave interval),
  `autosave.sfproj` (every N minutes while dirty, default 5, removed once the work is saved) and
  `clean_exit` (touched on normal exit). An autosave newer than `clean_exit` is offered for restore.
