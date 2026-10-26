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

The CRKW Windows Release build is staged in `out/crkw-app/`. Desktop and per-user
Start menu shortcuts named **SculptForge CRKW** launch `out/crkw-app/bin/sculptforge.exe`.
The application window and About display identify it as CRKW.

### CrokedWillo workspace

The CrokedWillo tab is immediately after Help, last in the left-aligned palette
bar. CRKW Clay Polish offers smoothing and curvature sharpening with independent
strengths and pass counts, shape correction, and tangential edge smoothing.
It preserves topology, protects masked and hidden vertices, supports sculpt
layers, and records both stages as one undo step. Disable Dynamic topology
before using it. This is a CRKW approximation of the Sculpt Helper workflow,
not a direct port of Blender's mesh-filter implementation.

The Reset Clay Polish button restores all six filter values without modifying
the mesh or its undo history. Background controls offer a solid colour or a
two-colour gradient, with a reset to the upstream colours. The optional silhouette
shows all visible SubTools from the current view direction, framed independently
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

The Draw Size popup closes on mouse/pen release, not transient widget
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
`ui_lightbox` failed on a QuickSave help message;
`ui_zbrush_feel` failed on negative Focal Shift entry. The official 0.8.1 portable
release also fails the Focal Shift check at the same test line. Full local logs:
`out/crkw-build.log`, `out/crkw-tests.log`,
`out/crkw-appearance-tests.log`, and `out/crkw-startup.log`.

The official Windows portable download is stored under `out/ckw-downloads/`;
`out/` is already ignored. This is an upstream baseline executable, not a CRKW
custom build. A source build requires CMake 3.24+ and a C++20 compiler; see
README.md and CONTRIBUTING.md for the full build and test instructions.

Internal `ckw` source identifiers, the existing `ckw/crookedtools` branch and
`ckw_clay_polish` script command are retained for compatibility. New scripts can
use `crkw_clay_polish`. Public app, shelf and shortcut branding uses CRKW.

GitHub browser sign-in does not authenticate command-line Git. If a push asks
for authentication, complete Git Credential Manager's normal sign-in locally;
do not put passwords or tokens in repository files or remote URLs.
