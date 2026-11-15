# CRKW CrookedTools SculptForge

Fork: https://github.com/LiamQ4/SculptForge
Upstream: https://github.com/islandmed/SculptForge

## Branches and naming

Keep `main` aligned with upstream. Build the custom app on `ckw/crookedtools`.
Include CRKW in custom feature names, documentation, and custom build branding.
Use focused commits and feature branches such as `ckw/sculpt-helper`.
The initial upstream baseline is commit `838fa79`; release tags are fetched locally.

`origin` is our fork. `upstream` fetches the original project; its push URL is
deliberately disabled locally. Default pushes go to origin. Fast-forward-only
pulls avoid accidental merge commits; recorded conflict resolutions are enabled.

## Merge future upstream updates

Start with a clean working tree; commit current work before updating.

```powershell
git fetch upstream --tags
git switch main
git merge --ff-only upstream/main
git push origin main
git switch ckw/crookedtools
git switch -c ckw/update-upstream-YYYY-MM-DD
git merge main
# Resolve conflicts, build, and run the test suite before accepting the update.
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
git switch ckw/crookedtools
git merge --ff-only ckw/update-upstream-YYYY-MM-DD
git push origin ckw/crookedtools
```

Replace the date placeholder with the update date. If the merge needs to be
cancelled, use `git merge --abort` while it is in progress. Never reset the CRKW
branch to upstream or force-push it to perform an update.
For release-only adoption, merge a reviewed upstream release tag into an update
branch from `ckw/crookedtools` instead of merging `main`.

## CrookedTools integration direction

The existing Blender Sculpt Helper lives in the sibling `Sculpt Helper` folder.
It uses Blender sculpt filters; those operations need a deliberate C++ port or
integration through the existing Blender bridge, rather than copying Python
into the standalone application.

Keep new operations in dedicated CRKW source files where practical, with small
integration changes to the upstream UI. Follow CONTRIBUTING.md: expose editor
operations through the core command registry and keep core code free of UI/GPU
dependencies. Cover sculpt behavior and UI integration with the existing test
systems. Preserve upstream license and attribution files.

## Local app and build

### Upstream v0.12.16 integration — 2026-11-15

Source reviewed: 2026-11-15. Release-only adoption starts from CRKW `c3827b6`,
upstream baseline `e55d3e0` (0.10.7). Fetched upstream tag `v0.12.16` at
`11f6a536bdaae66e8b069643fb7f25a4d14ed197`, merged on
`ckw/update-upstream-2026-11-14`. Production C++ source/proof revision is `a04991a6`; clipboard fixture commit is `49ca8f56`;
subsequent documentation commits do not change the tested executable. The two
untracked `AGENTS.md` instruction files remain outside commits. No stock binary is used.

The merge adapts every CRKW tool to Parts/PartData, Face Groups, Vertex Paint and
Lit Sphere APIs. Thirteen custom materials keep their old indices; imported Gold
becomes Gold 2. Layout version 6 migrates upstream view toggles from CRKW version 5,
retains Camera/Masks and fits compact right shelves. Held resize, consent at 200%,
clipboard image routing and native PNG/DIB/DIBV5 tests pass focused checks. Maya
exchange tests check toolbar overlap instead of assuming updater controls occupy it.
Project migration preserves artist-owned CRKW payload keys. Linked Parts retain
MetaHuman identity through creation/reload; mask state survives undo and subdivision.
Silhouettes frame transformed linked instances. Native dialogs are suppressed in UI
tests. Fixture paths expand after parsing so workspace spaces remain safe.

Build from the repository root with the local CMake (Visual Studio Release):

```powershell
$env:CL = '/MP8'
& out/build-tools/cmake/data/bin/cmake.exe -S . -B build-ckw '-DSCULPTFORGE_UPDATE_CHECK=OFF' '-DSCULPTFORGE_DEV_BUILD=OFF' '-DSCULPTFORGE_STALE_BASE=v0.12.16'
& out/build-tools/cmake/data/bin/cmake.exe --build build-ckw --config Release --parallel 8
$env:SF_THREADS = '2'
$env:SF_TIME_LIMIT_SCALE = '4'
$env:SF_USER_DATA_DIR = Join-Path $PWD 'out/update-01216-ctest-user'
& out/build-tools/cmake/data/bin/ctest.exe --test-dir build-ckw -C Release -LE nightly --output-on-failure -j4
```

Plugins/Wasmtime and WebRTC remain enabled with upstream pinned dependencies.
Do not set `SF_AI=off` for the full suite: policy tests exercise consent state.
`SF_USER_DATA_DIR` isolates all app settings; `SF_TEST_SETTINGS_DIR` remains a legacy
override. Windows clipboard-dependent scripts share a CTest resource lock.
`SF_TIME_LIMIT_SCALE=4` is upstream's parallel-run timing allowance; run affected
strict timing checks separately with `SF_BENCH_LIMITS=1`. The stale-tree audit uses
the adopted release tag; its default remains `origin/main` for upstream workflows.
Automatic and manual stock updates are disabled. Six updater/member-installer UI
cases are disabled for this configuration, alongside upstream's disabled stroke
resize case. Nine nightly cases are outside the regular run. The dedicated
`crkw_updates_disabled.sfui` verifies the manual-update interface.

CRKW text has entries in all four upstream translations and uses `tr()` for labels.
Coverage checks report 3,539 interface strings, 50 intentionally English, 3,489
translated and zero raw labels. Regenerated CJK subsets use upstream's
`tools/i18n/subset_cjk_font.py`, Noto CJK 2.004, and workspace-only FontTools 4.68.0.
Font license/notices are unchanged. Native i18n passed 12 cases/204 checks; CRKW
passed 19 cases/3,380 checks and CKW polish 3 cases/988 checks.

Pre-replacement backup: `out/update-01216-pre-0107`, all 15 installation files
verified against `out/update-01216-backup-manifest.json`. User preferences snapshot:
`out/update-01216-preferences-snapshot`, hashes in
`out/update-01216-preferences-before.json`. Desktop shortcut proof is in
`out/update-01216-shortcut.json`; target is `out/crkw-app/bin/sculptforge.exe`.
Installed version is now **CRKW 0.12.16** at the verified shortcut target. Both executable hashes match the tested Release (`out/update-01216-installed-artifacts.json`); GUI file/product versions are 0.12.16.
Install the tested Release only with the local CMake `--install build-ckw --config
Release --prefix out/crkw-app`; compare executable hashes and both version fields,
then run installed UI smoke checks under isolated settings. Never stop an open
application with unsaved work. `out/crkw-metahuman-app` remains a junction to this prefix.

Logs: `out/crkw-01216-final-configure.log`, `out/crkw-01216-final-build.log`,
`out/crkw-01216-production-tests.log`. Earlier failed/targeted logs remain historical
under the same prefix and are not represented as clean full-suite results.

Historical entries below retain their versions, counts and log paths; terminology was normalized on 2026-11-14 to match the current UI.

### Upstream 0.9.4 integration (2026-10-30)

Current CRKW camera, mask manager, automatic retopology, MetaHuman and texture
work was checkpointed in `eb094f7` before merging release tag `v0.9.4` on
`ckw/update-upstream-2026-10-30`.

The material conflict combines upstream Soft Clay with all CRKW materials and
toon shading. Soft Clay is appended after the existing CRKW materials, preserving
all previously saved material indices; the picker lists Soft Clay first.
The background conflict keeps CRKW's editable solid/gradient shader, colours
and reset behavior. Upstream shelf sizes, interface changes, phone/AR fixes and
0.9.4 build/export fixes are included. Update notifications remain disabled.

Validation: the Windows Release build succeeded with WebRTC enabled. All 57,402
core checks passed, as did every CRKW UI test. CTest passed 59/61 entries; only
the previously documented the retired library UI test QuickSave help-text check and
the retired classic-input UI test negative brush focus input check failed, at the same assertions
as before. All six phone/AR relay tests also passed.

Installed to `out/crkw-app` and verified that the installed executable is
byte-identical to the validated build and reports file/product version 0.9.4.
An installed-app UI smoke test passed with isolated settings; screenshots verify
the CRKW workspace and the 0.9.4 What's New chapter. The prior executable is
backed up at `out/sculptforge-crkw-pre-094.exe`.
Logs: `out/crkw-094-configure.log`, `out/crkw-094-build.log`,
`out/crkw-094-tests.log`, `out/crkw-094-link-tests.log`,
`out/crkw-094-install.log`, and `out/crkw-094-installed-smoke.log`.

### Upstream 0.9.1 integration (2026-10-27)

Local Lit Spheres, symmetric mask shapes, clipboard image paste and isolated UI-test
settings were saved in commit `24375d8` before merging upstream tag `v0.9.1`.
The merge completed without textual conflicts. The Windows Release build succeeded
with WebRTC enabled and upstream update notifications disabled.

Validation: all 54,699 core checks and every CRKW UI test passed. Of the 55 CTest
entries, 53 passed after two test compatibility fixes: the one-sided lasso test
explicitly disables X symmetry, and the face-shape test quotes paths with spaces.
The two previously documented UI failures remain: the library's QuickSave help-message
expectation and negative brush focus keyboard input in the retired classic-input UI test.
Logs: `out/crkw-091-build.log`, `out/crkw-091-tests.log`,
`out/crkw-091-mask-retest.log`, and `out/crkw-091-face-retest.log`.

The CRKW Windows Release build is staged in `out/crkw-app/`, the only install folder. Desktop and per-user
Start menu shortcuts named **SculptForge CRKW** launch `out/crkw-app/bin/sculptforge.exe`.
`out/crkw-metahuman-app` is a directory junction to `out/crkw-app` (2026-10-26), so a taskbar pin
made from the old path also opens the latest build. Never install to another prefix.
The application window and About display identify it as CRKW.

### CrokedWillo workspace

The right shelf's **Camera** button opens the compact CRKW camera controller.
Persp/Ortho preserves the target-plane framing; orthographic picking and brush
size use parallel projection. Lens mm and vertical FOV are linked using a
24 mm full-frame sensor height. Fixed canvas fits the chosen aspect into the
available viewport; width/height define exact PNG export pixels (64-8192 per
side, at most 16 megapixels). Presets, portrait swap, Frame, and reset are in the
same panel. Camera projection, lens, canvas size and fixed mode save in projects.
Older stored shelf layouts gain the Camera button without replacing custom items.

The CrokedWillo tab is immediately after Help, last in the left-aligned palette
bar. CRKW Clay Polish offers smoothing and curvature sharpening with independent
strengths and pass counts, shape correction, and tangential edge smoothing.
It preserves topology, protects masked and hidden vertices, supports sculpt
layers, and records both stages as one undo step. Disable Dynamic topology
before using it. This is a CRKW approximation of the Sculpt Helper workflow,
not a direct port of Blender's mesh-filter implementation.

CRKW Mesh Sharpen (below Clay Polish; script command `crkw_mesh_sharpen`) is a port of the
Sculpt Helper add-on's Mesh Sharpen geometry-nodes filter (`src/core/CkwMeshSharpen.cpp`):
curvature-weighted smoothing that flattens planes and tightens creases. Curvature is measured
once, then Iterations passes pull each vertex toward its neighbours weighted by their curvature;
Blend mixes with the input, Pin Borders keeps open edges fixed, Curvature is Mean or Curvedness.
Like the classic workflow it works on the current subdivision level, and it respects masks, hidden vertices
and sculpt layers (one undo step). Not yet compared numerically with Blender's output.

The Reset Clay Polish button restores all six filter values without modifying
the mesh or its undo history. Background controls offer a solid colour or a
two-colour gradient, with a reset to the upstream colours. The optional silhouette
shows all visible Parts from the current view direction, framed independently
of main-view pan/zoom. Its size and top-left/top-right placement are adjustable.
It reuses uploaded GPU geometry, renders into a small persistent texture without
CPU pixel readback, and refreshes at most about seven times per second.

The compact Settings dropdown inside CrokedWillo contains fullscreen, monitor,
and Huion startup controls. F11 also toggles borderless windowed fullscreen.
The preferred display defaults to Kamvas 22plus. Change it in CrokedWillo;
display, mode, and Huion startup preferences save automatically in
`%APPDATA%/SculptForge/SculptForge/crkw-workspace.json`. Existing `ckw-window.json`
preferences are loaded when the new file is absent. A disconnected display falls
back to the primary monitor without discarding the preferred display name.

On Windows CRKW checks for HuionTablet at startup and launches the installed
`Program Files/HuionTablet/HuionTablet.exe` if absent. Huion may itself request
administrator consent; CRKW does not bypass it or create elevated tasks. Turn
off auto-start or open Huion manually from CrokedWillo. Headless tests, renders,
and recordings never launch Huion or change desktop window settings.

The brush size popup closes on mouse/pen release, not transient widget
deactivation. The legacy `CKW-SIZE-DRAG` comments in Pickers.cpp preserve the original
upstream close logic for review/removal if upstream supplies an equivalent fix.

Rebuild and refresh the executable (close the app first):

```powershell
cmake -S . -B build-ckw -DSCULPTFORGE_UPDATE_CHECK=OFF
cmake --build build-ckw --config Release --parallel 8
ctest --test-dir build-ckw -C Release --output-on-failure
cmake --install build-ckw --config Release --prefix out/crkw-app
```

Upstream update notifications are disabled for this build: adopt upstream changes
through the merge workflow above and rebuild the CRKW executable.

Current Windows Release validation: build succeeded, app startup was responsive
on Kamvas 22plus at 1920x1080 in borderless windowed mode (exclusive fullscreen
off), and 45/47 CTest checks passed (including core and Blender exchange/UI
checks). The CRKW workspace and appearance checks validate drag-to-release,
filter reset, solid/gradient backgrounds, silhouette toggle/size/corner, and
the Settings dropdown. CRKW geometry tests passed 988 checks.
the retired library UI test failed on a QuickSave help message;
the retired classic-input UI test failed on negative brush focus entry. The official 0.8.1 portable
release also fails the brush focus check at the same test line. Full local logs:
`out/crkw-build.log`, `out/crkw-tests.log`,
`out/crkw-appearance-tests.log`, and `out/crkw-startup.log`.

The official Windows portable download is stored under `out/ckw-downloads/`;
`out/` is already ignored. This is an upstream baseline executable, not a CRKW
custom build. A source build requires CMake 3.24+ and a C++20 compiler; see
README.md and CONTRIBUTING.md for the full build and test instructions.

Native **CrokedWillo > MetaHuman Sculpt** tools support local template creation,
guide previews, base UV repair and separate-part FBX export. See
[the CRKW MetaHuman workflow](docs/crkw-metahuman.md) for setup and limitations.
Template assets remain local and are not redistributed with the fork.

Internal `ckw` source identifiers, the existing `ckw/crookedtools` branch and
`ckw_clay_polish` script command are retained for compatibility. New scripts can
use `crkw_clay_polish`. Public app, shelf and shortcut branding uses CRKW.

GitHub browser sign-in does not authenticate command-line Git. If a push asks
for authentication, complete Git Credential Manager's normal sign-in locally;
do not put passwords or tokens in repository files or remote URLs.


### Additional integration checks — 2026-11-15

All eight core shards passed, 1,580 cases and 591,742 checks. Blender 5.2.1's isolated
bridge test passed 192 checks; legacy 0.10.7 camera and MetaHuman/custom-mask projects
passed load/save/reload UI checks. All 15 pre-update installation files were verified against the
backup at `out/update-01216-pre-0107`. User preference hashes still match the pre-test snapshot.

Completed UI reconciliation covers the held resize gesture, native clipboard shortcut routing,
right-shelf view folding, consent at 200% scale, custom Gold 2 expectations and translated
CRKW labels. Configure production display with `SCULPTFORGE_DEV_BUILD=OFF`, update checks
OFF, and `SCULPTFORGE_STALE_BASE=v0.12.16` (the default upstream audit base remains
`origin/main`). Updater/member-installer-dependent UI scripts are disabled only in the
update-check-OFF configuration; `crkw_updates_disabled.sfui` verifies that interface.
Run parallel timing checks with upstream's `SF_TIME_LIMIT_SCALE=4`; rerun strict affected
performance checks alone with `SF_BENCH_LIMITS=1`.

The initial background Blender test omitted `BLENDER_USER_EXTENSIONS`, triggering shared
Pillow wheel cleanup. The exact local wheel's DLL matched the original SHA-256 and all
120 wheel files were restored and verified. The successful repeat explicitly isolated
`BLENDER_USER_RESOURCES`, CONFIG, SCRIPTS, EXTENSIONS and DATAFILES and verified its
extension root before enabling the source bridge. It left the restored shared cache unchanged.

Final focused production checks passed: 15 popup/pen/card/update-disabled UI cases,
ten consecutive native Windows clipboard runs, and Auto UV's original strict timing
limit (`SF_BENCH_LIMITS=1`, isolated). Source/proof revision `a04991a6` contains the C++
changes; `49ca8f56` only stabilizes the clipboard fixture's palette-opening timing.
The final complete Release suite passed **555/555 enabled non-nightly tests**, zero failures, in 777.31 seconds. Seven tests were disabled and nine nightly tests were excluded. The installation completed successfully after checking no installed app was running and re-verifying the full backup. No application was terminated. Installed smoke checks are recorded separately below.

Tested GUI SHA-256: `7BDBD4494808CD5B3DDBBA0129474838020C9ABAFE2AC1E99953722087BBD539`. The CLI hash and source/proof revision are recorded in `out/update-01216-tested-build.json`. Final full-suite log: `out/crkw-01216-production-tests.log`; strict timing: `out/crkw-01216-strict-uv.log`; clipboard repetition: `out/crkw-01216-clipboard-repeat.log`. Earlier failure logs remain historical.

Installed verification: **15/15 UI checks passed**, including every CRKW UI script, version/title and original 0.10.7 camera/MetaHuman-mask projects. Installed CLI Clay Polish and Mesh Sharpen passed. All **12 user preference files are unchanged**, shortcut target verified, and no app was terminated. Evidence: `out/update-01216-installed-smoke.json`, `out/crkw-01216-installed-cli.log`, `out/update-01216-final-verification.json`. The source and installed release versions are both 0.12.16; the installed Blender add-on was not replaced.

Remaining limits: nine nightly heavy-mesh/fuzz/extended benchmark tests were excluded; native Blender 5.3+ splat exchange, live Maya/other sculpting application round trips and Unreal MetaHuman conform were not run. MetaHuman checks use synthetic fixtures. The background Blender check passed 192 assertions; both bridge 1.0.0 ZIP variants passed packaging checks.
