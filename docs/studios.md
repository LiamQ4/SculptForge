# SculptForge for studios and pipelines

SculptForge is built to drop into animation, VFX and game studios that run their own pipelines:
Linux-first, scriptable, standards-based, with no licence server and nothing to phone home.

## VFX Reference Platform

SculptForge tracks the [VFX Reference Platform](https://vfxplatform.com). CI builds and tests every commit
in the platform's reference environments:

| | CY2026 (current) | CY2027 | SculptForge |
|---|---|---|---|
| Linux compiler | GCC 14.2 | GCC 14.2 | Built and tested with `gcc-toolset-14` |
| glibc | 2.28 (EL8) | 2.34 (EL9) | Rocky Linux 8 and 9 builds; CI verifies no newer glibc symbols are used |
| C++ | C++20 | C++20 | C++20 |
| Python | 3.13 | 3.13 | `import sculptforge` module built and tested against 3.13 |
| Windows | VS 2022 17.6+ | VS build tools 14.44+ | Visual Studio 2022 |
| macOS minimum | 14.0 | 14.0 | Universal binary, deployment target 11.0 (runs on 14.0+) |
| FBX | 2020.x | 2020.x | Writes FBX 7.4 binary, which FBX SDK 2020 reads |

SculptForge doesn't depend on Qt, OpenEXR, Boost, TBB or other platform libraries, so it can't conflict
with the versions your other tools load. Its few dependencies (SDL3, Dear ImGui, GLM, ufbx) are linked
statically into the binaries.

Each release includes `SculptForge-<version>-linux-<arch>-vfx2026.tar.gz` (Rocky 8, glibc 2.28) and
`…-vfx2027.tar.gz` (Rocky 9, glibc 2.34), with the Python module under `lib/sculptforge/python`.

## Deploying with Rez

`packaging/rez/package.py` builds and installs a Rez package:

```sh
cd packaging/rez
SCULPTFORGE_PYTHON=$(which python3.13) rez-build --install     # or --release for your release repository
rez-env sculptforge -- sculptforge                              # the app
rez-env sculptforge -- sculptforge-cli --list                   # every pipeline command
rez-env sculptforge python-3.13 -- python -c "import sculptforge"
```

The package puts `bin/` on `PATH`, puts the Python module on `PYTHONPATH`, and sets `SCULPTFORGE_ROOT`.

## Python API

```python
import sculptforge as sf

doc = sf.Document()
sf.run(doc, "open", path="/show/assets/troll/troll_base.fbx")
sf.run(doc, "remesh", resolution=320)
sf.run(doc, "export_usd", path="/show/assets/troll/troll_sculpt.usda")

verts, tris = doc.vertices, doc.triangles     # NumPy (N, 3) float32 and (M, 3) uint32
doc.set_mesh(my_vertices, my_triangles)      # bring meshes in from your own tools
for name, weight in doc.layers:              # sculpt layers / blend shapes
    print(name, weight, doc.layer_delta(0).shape)

sf.run_script(doc, "decimate ratio=0.25; export_lods path=/out/troll.glb levels=4")
print([c["name"] for c in sf.commands()])     # everything the app can do
```

Failures raise `sculptforge.CommandError`. Long operations release the GIL.

## Headless and farm use

- `sculptforge-cli` runs every editor operation without a display: imports, remeshing, decimation, LODs,
  FBX/USD/glTF export, splat conversion, AI generation.
- `sculptforge --render out.png --turntable 8` renders review images and turntables offscreen.
- `sculptforge-cli --mcp` exposes the same operations to AI agents through the Model Context Protocol.

## Formats in your pipeline

| Format | Read | Write | Notes |
|---|---|---|---|
| OpenUSD (`.usda`, `.usdz`) | ✓ | ✓ | Meshes, xform ops, Z-up stages, UsdSkel blend shapes, ParticleField Gaussian splats; checked with OpenUSD 26.08 `usdchecker` |
| FBX | ✓ (any version, via ufbx) | ✓ (7.4 binary) | LOD groups, blend shapes, cm units |
| glTF / GLB | ✓ | ✓ | Morph targets, `KHR_gaussian_splatting`; checked with the Khronos validator |
| OBJ, PLY | ✓ | ✓ (OBJ) | PLY meshes, point clouds and splats |

Binary `.usdc` and USD references/payloads aren't read yet; convert with `usdcat in.usd -o out.usda`.

## Licensing for studios

SculptForge is GPL-3.0-or-later. In practice:

- **Using it is unrestricted**, commercially or otherwise, on any number of machines. There are no seats,
  licence servers or accounts.
- **Your artwork belongs to you.** The GPL covers the program, not the models you make with it.
- **You can modify it in-house.** Fork it and wire it into your asset system. The GPL only requires you to
  share source if you *distribute* the modified program outside your organisation. Internal use, including
  across your own facilities and farm, isn't distribution. (This is the common reading of the GPL, not
  legal advice; check with your counsel.)
- Contributions back upstream are welcome, but never required for internal use.

## Roadmap items for pipelines

Planned, roughly in this order: OpenColorIO viewport display, Alembic export, fuller USD authoring (layers,
variants, references, `.usdc`), multiresolution sculpting and masking. Pipeline requests are welcome as issues.
