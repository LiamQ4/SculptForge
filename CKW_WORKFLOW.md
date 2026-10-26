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
The application window and About display identify it as CKW. This initial build
retains upstream sculpting behavior; CKW Clay Polish is not implemented yet.

Rebuild and refresh the executable (close the app first):

```powershell
cmake -S . -B build-ckw -DSCULPTFORGE_UPDATE_CHECK=OFF
cmake --build build-ckw --config Release --parallel 8
ctest --test-dir build-ckw -C Release --output-on-failure
cmake --install build-ckw --config Release --prefix out/ckw-app
```

Upstream update notifications are disabled for this build: adopt upstream changes
through the merge workflow above and rebuild the CKW executable.

Initial Windows Release validation: build succeeded, app startup was responsive
with the CKW title, and 43/45 CTest checks passed (including core and Blender
exchange/UI checks). `ui_lightbox` failed on a QuickSave help message;
`ui_zbrush_feel` failed on negative Focal Shift entry. The official 0.8.1 portable
release also fails the Focal Shift check at the same test line. Full local logs:
`out/ckw-build.log` and `out/ckw-tests.log`.

The official Windows portable download is stored under `out/ckw-downloads/`;
`out/` is already ignored. This is an upstream baseline executable, not a CKW
custom build. A source build requires CMake 3.24+ and a C++20 compiler; see
README.md and CONTRIBUTING.md for the full build and test instructions.

GitHub browser sign-in does not authenticate command-line Git. If a push asks
for authentication, complete Git Credential Manager's normal sign-in locally;
do not put passwords or tokens in repository files or remote URLs.
