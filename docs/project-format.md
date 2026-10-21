# SculptForge project files (.sfproj)

A `.sfproj` is an uncompressed ("stored") ZIP (src/io/Zip.cpp) written by `saveProject` /
read by `loadProject` (src/core/Project.cpp). Commands: `save_project path=...`,
`open_project path=...`; `open` also recognises `.sfproj`.

## Layout
- `manifest.json`: always first. `format` = `"SculptForge project"`, `version` (integer, currently 1),
  `app_version`, then `mesh`, `layers`, `active_layer`, `brushes` (recipe JSON), `active_brush`
  (name), optional `splats`, and app-owned keys (`camera`, `render`, `brush`, `symmetry`).
- Blobs are little-endian arrays. The manifest describes each one as
  `{"file", "type": "f32"|"u32"|"u8", "components", "count"}`; readers check all four.

| Manifest key | File | Layout |
|--------------|------|--------|
| `mesh.positions` | mesh/positions.bin | f32 x3 per vertex (current blend) |
| `mesh.triangles` | mesh/triangles.bin | u32 x3 per triangle |
| `mesh.base` | mesh/base.bin | f32 x3 per vertex (only with layers) |
| `mesh.mask` / `colors` | mesh/mask.bin, colors.bin | f32 x1 / x3 per vertex (linear RGB) |
| `mesh.polygroups` / `hidden` | mesh/polygroups.bin, hidden.bin | u32 / u8 per triangle |
| `mesh.uvs` | mesh/uvs.bin | f32 x2 per triangle corner |
| `layers[i].delta` | layers/<i>.bin | f32 x3 per vertex, plus `name`, `weight` |
| `splats.*` | splats/*.bin | positions x3, rotations x4 (x,y,z,w), scales x3, opacities x1, sh x3 per coefficient; plus `count`, `sh_degree`, `name`, `transform` (16 floats, column-major) |

Optional attributes are simply absent when the mesh doesn't have them.

## Compatibility rules
- Unknown keys and files are ignored. Unknown **top-level** keys are kept in
  `Document::projectSettings` and written back, so older builds don't drop newer settings.
- A file whose `version` is greater than `kProjectVersion` is refused with a message naming the app
  version that wrote it. Bump `kProjectVersion` only for changes old readers would misread;
  adding a new optional blob does not need a bump.
- New mesh attributes: add a blob in `saveProject` and read it in `loadProject` (size-checked).

## App behaviour (src/app/PanelProject.cpp)
- `Document::dirty()` = any undo push/undo/redo, splat edit or `modified` since `markClean()`
  (save, open, new). The window title shows `*` while dirty.
- New / Open / Open Recent / drop / Quit / window close ask Save / Don't Save / Cancel when dirty
  (never in `--render`, `--record` or demos).
- The pref dir (`SDL_GetPrefPath`) holds `project.json` (10 recent files, autosave interval),
  `autosave.sfproj` (every N minutes while dirty, default 5, removed once the work is saved) and
  `clean_exit` (touched on normal exit). An autosave newer than `clean_exit` is offered for restore.
