# CKW CrookedTools SculptForge

Fork: https://github.com/LiamQ4/SculptForge
Upstream: https://github.com/islandmed/SculptForge

## Branches and naming

Keep `main` aligned with upstream. Build the custom app on `ckw/crookedtools`.
Include CKW in custom feature names, documentation, and custom build branding.
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
cancelled, use `git merge --abort` while it is in progress. Never reset the CKW
branch to upstream or force-push it to perform an update.
For release-only adoption, merge a reviewed upstream release tag into an update
branch from `ckw/crookedtools` instead of merging `main`.

## CrookedTools integration direction

The existing Blender Sculpt Helper lives in the sibling `Sculpt Helper` folder.
It uses Blender sculpt filters; those operations need a deliberate C++ port or
integration through the existing Blender bridge, rather than copying Python
into the standalone application.

Keep new operations in dedicated CKW source files where practical, with small
integration changes to the upstream UI. Follow CONTRIBUTING.md: expose editor
operations through the core command registry and keep core code free of UI/GPU
dependencies. Cover sculpt behavior and UI integration with the existing test
systems. Preserve upstream license and attribution files.

## Local app and build

The CKW Windows Release build is staged in `out/ckw-app/`. Desktop and per-user
Start menu shortcuts named **SculptForge CKW** launch `out/ckw-app/bin/sculptforge.exe`.
The application window and About display identify it as CKW.

### CrokedWillo workspace

The CrokedWillo tab is immediately after Help, last in the left-aligned palette
bar. CKW Clay Polish offers smoothing and curvature sharpening with independent
strengths and pass counts, shape correction, and tangential edge smoothing.
It preserves topology, protects masked and hidden vertices, supports sculpt
layers, and records both stages as one undo step. Disable Dynamic topology
before using it. This is a CKW approximation of the Sculpt Helper workflow,
not a direct port of Blender's mesh-filter implementation.

F11 and the CrokedWillo window checkbox toggle borderless windowed fullscreen.
The preferred display defaults to Kamvas 22plus. Change it in CrokedWillo;
display, mode, and Huion startup preferences save automatically in
`%APPDATA%/SculptForge/SculptForge/ckw-window.json`. A disconnected display falls
back to the primary monitor without discarding the preferred display name.

On Windows CKW checks for HuionTablet at startup and launches the installed
`Program Files/HuionTablet/HuionTablet.exe` if absent. Huion may itself request
administrator consent; CKW does not bypass it or create elevated tasks. Turn
off auto-start or open Huion manually from CrokedWillo. Headless tests, renders,
and recordings never launch Huion or change desktop window settings.

The Draw Size popup closes on mouse/pen release, not transient widget
deactivation. The `CKW-SIZE-DRAG` comments in Pickers.cpp preserve the original
upstream close logic for review/removal if upstream supplies an equivalent fix.

Rebuild and refresh the executable (close the app first):

```powershell
cmake -S . -B build-ckw -DSCULPTFORGE_UPDATE_CHECK=OFF
cmake --build build-ckw --config Release --parallel 8
ctest --test-dir build-ckw -C Release --output-on-failure
cmake --install build-ckw --config Release --prefix out/ckw-app
```

Upstream update notifications are disabled for this build: adopt upstream changes
through the merge workflow above and rebuild the CKW executable.

Current Windows Release validation: build succeeded, app startup was responsive
on Kamvas 22plus at 1920x1080 in borderless windowed mode (exclusive fullscreen
off), and 44/46 CTest checks passed (including core and Blender exchange/UI
checks). The final CKW workspace, tablet, and Blender bridge UI checks all
passed after the last rebuild; new CKW geometry tests passed 988 checks.
`ui_lightbox` failed on a QuickSave help message;
`ui_zbrush_feel` failed on negative Focal Shift entry. The official 0.8.1 portable
release also fails the Focal Shift check at the same test line. Full local logs:
`out/ckw-build-final.log`, `out/ckw-tests.log`,
`out/ckw-final-targeted-tests.log`, and `out/ckw-startup.log`.

The official Windows portable download is stored under `out/ckw-downloads/`;
`out/` is already ignored. This is an upstream baseline executable, not a CKW
custom build. A source build requires CMake 3.24+ and a C++20 compiler; see
README.md and CONTRIBUTING.md for the full build and test instructions.

GitHub browser sign-in does not authenticate command-line Git. If a push asks
for authentication, complete Git Credential Manager's normal sign-in locally;
do not put passwords or tokens in repository files or remote URLs.
