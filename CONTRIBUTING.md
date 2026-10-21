# Contributing to SculptForge

Thanks for your interest! SculptForge is GPL-3.0-or-later; by contributing you
agree your work is licensed under the same terms.

## Ground rules

- **Clean-room only.** Do not contribute code, shaders, brushes, alphas, icons or
  other assets copied or decompiled from ZBrush or any other proprietary
  product. Re-implementing well-known published techniques is fine.
- Keep `src/core` free of UI and GPU dependencies so it stays testable and
  scriptable.
- New editor operations should be registered as commands in
  `src/core/Commands.cpp` so they are available to the UI, scripting and the
  AI assistant alike.
- Add or update tests in `tests/` for core behaviour changes.

## Workflow

1. Build and run tests (see README).
2. Keep changes focused; one feature or fix per pull request.
3. Each source file starts with the SPDX license header.
4. CI must pass on Linux, macOS and Windows.

## Code style

C++20, 4-space indentation, `camelCase` functions, `PascalCase` types,
trailing underscore for private members. A `.clang-format` is provided.

## Building, testing and packaging

See [README.md#building](README.md#building). On Linux, `packaging/linux/install-build-deps.sh` installs the
build dependencies. `ctest` runs the unit tests; `cpack` in the build folder makes the platform's packages.
Releases are built by `.github/workflows/release.yml` when a `v*` tag is pushed.

## Demo videos

The website's videos are real recordings made by the app itself:

```sh
./build/sculptforge --demo sculpt --record website/media/sculpt.mp4 --size 1920x1080 --demo-output creature.glb
./build/sculptforge --demo game-ready --record website/media/game-ready.mp4 --size 1920x1080 --demo-asset creature.glb
```

Demos live in `src/app/Demo.cpp`; they drive the real UI (buttons are found by their labels).

## Community

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security issues
privately, as described in [SECURITY.md](SECURITY.md).
