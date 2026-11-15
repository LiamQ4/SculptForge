# SculptForge Bridge — Blender add-on

Source reviewed: 2026-11-15. Active package: `sculptforge_bridge/`; source metadata version 1.0.0, Blender 4.2+. This is the Blender integration inside the larger SculptForge application, whose release version can differ.

Select meshes in Blender, use **Send to SculptForge**, sculpt there, then send back and receive in Blender. Controls appear under N sidebar > SculptForge and in object/file menus. Read [the integration documentation](../../docs/blender.md) and [user guide](../../docs/guide/blender.md) for preferences, exchange setup, compatibility and limitations.

## Source and data flow

- `sculptforge_bridge/__init__.py`: mesh/attribute extraction, object updates, blend shapes, send/receive operators, preferences, polling, menus and registration.
- `sculptforge_bridge/exchange.py`: shared exchange folder/inbox/outbox, manifest revision/publication, Bridge serialization, topology and coordinate conversion, application discovery/launch and heartbeat handling.
- `sculptforge_bridge/classic_keys.py`: optional Blender sculpt keymap integration, brush assets and subdivision controls. Disabling must restore prior keymaps.
- `sculptforge_bridge/bridge_protocol.py`: shared binary format constants.
- `sculptforge_bridge/splats.py`: Gaussian splat exchange; requires Blender 5.3+ for native point clouds.
- `sculptforge_bridge/blender_manifest.toml`: extension metadata/permissions; keep its version consistent with `bl_info`.

Exchange files and manifests connect Blender to the native application. Existing objects are updated in place where supported; preserve their transforms, materials and modifier behavior. Changes to coordinates, topology matching, loop/corner correspondence, UV/color/mask/face-set data or protocol revisions require checking both sides. Do not equate a successful file write with a confirmed receive.

## Build and verification

From the SculptForge project root:

```powershell
python addons/blender/build_addon.py --out out/blender-addon --check
```

The builder emits extension and legacy ZIP variants, validates metadata and compiles packaged modules. `--version X.Y.Z` stamps packaged metadata. It uses an explicit file list: a README added beside the builder is not automatically included in releases.

`tests/blender/test_in_blender.py` exercises the Blender integration; inspect its required CLI argument and environment before running. See `docs/blender.md` for test details and headless limitations. For native protocol changes, follow the parent project's CMake/CTest instructions too. Background tests must isolate `BLENDER_USER_RESOURCES`, `BLENDER_USER_CONFIG`, `BLENDER_USER_SCRIPTS`, `BLENDER_USER_EXTENSIONS` and `BLENDER_USER_DATAFILES`; factory startup alone does not isolate extension-wheel cleanup. Application settings use `SF_USER_DATA_DIR`.

## Maintenance — 2026-11-02

Updated with application upstream main `e55d3e0` (0.10.7). Bridge source metadata remains 0.7.0; installed Blender version was not changed. Upstream updates in `sculptforge_bridge/` add Linux Flatpak launcher discovery and align UI wording with Parts, face groups and vertex paint; exchange data and keymaps are unchanged by this update; `build_addon.py` retains its explicit module list and version stamping. Both extension and legacy packages passed the builder `--check` (metadata validation and Python compilation). No live Blender round trip was run.

## Maintenance — 2026-11-15

Upstream v0.12.16 updates the bridge source to 1.0.0, adds splat exchange and renames the optional sculpt key module to `classic_keys.py`. The binary mesh protocol remains compatible; see the updated integration guide. Both extension and legacy ZIPs passed `build_addon.py --out out/blender-addon-01216 --check`. A separate, fully isolated Blender 5.2.1 LTS process passed 192 integration checks against the Release CLI: mesh exchange, topology, transforms, UVs, colors, masks, face groups, keymaps and VDM. Native Gaussian splat exchange requires Blender 5.3+ and was not exercised on this host; its older-version rejection was covered. Installed Blender add-ons and open Blender documents were not replaced.

The first background run omitted extension-path isolation and Blender cleaned its shared Pillow cache. The exact cached Pillow 12.2.0 wheel was restored, with all 120 files verified byte-for-byte; the isolated repeat left it unchanged. Logs and recovery hashes are recorded in [the application handoff](../../CKW_WORKFLOW.md).
