# SculptForge Bridge — Blender add-on

Source reviewed: 2026-11-02. Active package: `sculptforge_bridge/`; source metadata version 0.7.0, Blender 4.2+. This is the Blender integration inside the larger SculptForge application, whose release version can differ.

Select meshes in Blender, use **Send to SculptForge**, sculpt there, then send back and receive in Blender. Controls appear under N sidebar > SculptForge and in object/file menus. Read [the integration documentation](../../docs/blender.md) and [user guide](../../docs/guide/blender.md) for preferences, exchange setup, compatibility and limitations.

## Source and data flow

- `sculptforge_bridge/__init__.py`: mesh/attribute extraction, object updates, blend shapes, send/receive operators, preferences, polling, menus and registration.
- `sculptforge_bridge/exchange.py`: shared exchange folder/inbox/outbox, manifest revision/publication, GoZ serialization, topology and coordinate conversion, application discovery/launch and heartbeat handling.
- `sculptforge_bridge/zbrush_keys.py`: optional Blender sculpt keymap integration, brush assets and subdivision controls. Disabling must restore prior keymaps.
- `sculptforge_bridge/blender_manifest.toml`: extension metadata/permissions; keep its version consistent with `bl_info`.

Exchange files and manifests connect Blender to the native application. Existing objects are updated in place where supported; preserve their transforms, materials and modifier behavior. Changes to coordinates, topology matching, loop/corner correspondence, UV/color/mask/face-set data or protocol revisions require checking both sides. Do not equate a successful file write with a confirmed receive.

## Build and verification

From the SculptForge project root:

```powershell
python addons/blender/build_addon.py --out out/blender-addon --check
```

The builder emits extension and legacy ZIP variants, validates metadata and compiles packaged modules. `--version X.Y.Z` stamps packaged metadata. It uses an explicit file list: a README added beside the builder is not automatically included in releases.

`tests/blender/test_in_blender.py` exercises the Blender integration; inspect its required CLI argument and environment before running. See `docs/blender.md` for test details and headless limitations. For native protocol changes, follow the parent project's CMake/CTest instructions too. No live exchange or build was run for this documentation update.

## Maintenance — 2026-11-02

Updated with application upstream main `e55d3e0` (0.10.7). Bridge source metadata remains 0.7.0; installed Blender version was not changed. Upstream updates in `sculptforge_bridge/` add Linux Flatpak launcher discovery and align UI wording with Parts, face groups and vertex paint; exchange data and keymaps are unchanged by this update; `build_addon.py` retains its explicit module list and version stamping. Both extension and legacy packages passed the builder `--check` (metadata validation and Python compilation). No live Blender round trip was run.
