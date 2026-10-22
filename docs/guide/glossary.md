# Glossary

This chapter maps ZBrush terms to their SculptForge equivalents, so you can find a familiar tool quickly, and
explains the terms that only SculptForge uses. "Not yet" means SculptForge doesn't have it today; see the roadmap
for what is planned.

## ZBrush terms in SculptForge

| ZBrush | SculptForge |
|---|---|
| Alpha | **Alpha**: built-in round, square and noise, or any grayscale image. Left shelf, Alpha palette. See [Sculpting](sculpting.md) |
| Auto Groups | **Tool > Polygroups > By pieces** (`polygroup_by_connectivity`) |
| BPR render | Not yet as a renderer. Headless renders and turntables with `sculptforge --render` ([Scripting](scripting.md)) |
| Brush pop-up (B) | **Brush picker**, <kbd>B</kbd>, then type letters (B S T = Standard) |
| Clip brushes | Not yet. The **Trim** brush shaves to a plane |
| Curve brushes, IMM, ZModeler, ZSpheres | Not yet |
| Decimation Master | **Tool > Geometry > Decimate** (`decimate`). See [Geometry](geometry.md#decimate) |
| Del Hidden | **Tool > Visibility > Delete hidden** (`delete_hidden`) |
| Divide (Ctrl+D) | **Tool > Geometry > Subdivide**, <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| Document, 2.5D canvas, pixols | Not planned: SculptForge is a 3D sculpting app only |
| Draw Size | **Draw Size** (top shelf, <kbd>S</kbd>, <kbd>[</kbd> / <kbd>]</kbd>) |
| DynaMesh | **Voxel Remesh** (**Tool > Remesh**, `remesh`). See [Geometry](geometry.md#voxel-remesh) |
| Focal Shift | No slider for the built-in brushes. Custom brushes have **Hardness** and **Tip sharpness** in their recipe |
| Ghost, Transp | Not yet. Inactive SubTools are drawn slightly dimmed |
| GoZ | **GoZ**: `.GoZ` import and export and the GoZ bridge (**File > ZBrush (GoZ)**). See [Exporting](exporting.md) |
| Group Masked | **Tool > Polygroups > From mask** (`polygroup_from_mask`) |
| Group Visible | **Tool > Polygroups > Merge visible** (`polygroup_merge_visible`) |
| Groups By Normals | **Tool > Polygroups > By normals** with an angle (`polygroup_auto_by_normals`) |
| Layers (sculpt layers) | **Tool > Layers**: sculpt layers with weights, exported as blend shapes. See [Layers](layers.md) |
| LazyMouse | **LazyMouse** stroke type (Stroke palette) |
| LightBox | **LightBox**, <kbd>,</kbd> (comma) |
| Live Boolean | Not yet |
| Local symmetry, radial symmetry | Not yet. Symmetry is X, Y and Z across the world axes |
| Mask by Cavity | **Tool > Masking > By cavity** (`mask_by_cavity`) |
| Masking (Ctrl) | Hold <kbd>Ctrl</kbd> to paint the mask, <kbd>Ctrl</kbd>+<kbd>Alt</kbd> to erase. See [Masking](masking.md) |
| Mask Lasso, Select Lasso | Not yet. Rectangle hide works: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag |
| MatCap | **Material**: Studio plus six MatCaps (<kbd>M</kbd>). Custom MatCap images not yet. See [Polypaint](polypaint.md) |
| Morph target | A [sculpt layer](layers.md); exported as glTF morph targets and FBX/USD blend shapes |
| Mrgb, M (material channel) | Not yet: there is no per-vertex material channel |
| Polyframe (PolyF, Shift+F) | **PolyF** on the right shelf, <kbd>W</kbd> or <kbd>Shift</kbd>+<kbd>F</kbd> |
| Polygroups | **Polygroups**: same idea, same Ctrl+Shift gestures. See [Polygroups and visibility](polygroups.md) |
| Polypaint | **Polypaint**: Paint brush (<kbd>P</kbd>), Rgb, fills, cavity paint. See [Polypaint](polypaint.md) |
| Projection Master | Not yet. **Generate > Stamp** projects an image onto the model from the view |
| QuickSave | **QuickSave** in the title strip; the last 10 are kept. See [Projects](projects.md#quicksave) |
| Rgb | **Rgb** on the top shelf (paint while sculpting) |
| Rgb Intensity, Z Intensity | **Strength** (top shelf, <kbd>I</kbd>) |
| Sculptris Pro, dynamic topology | Not yet |
| SDiv, D / Shift+D (subdivision levels) | Not yet: Subdivide is a single, destructive step |
| Solo | **Solo** on the right shelf or in **Tool > SubTool** |
| Spotlight | Not yet |
| Store Config | **Store Config**, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> (**Preferences > Config**) |
| Stroke (Dots, DragRect, Spray) | **Stroke** types Dots, DragRect, Spray, LazyMouse |
| SubTool | **SubTool**: same idea, <kbd>Alt</kbd>+click to select. See [SubTools](subtools.md) |
| Texture palette | Not yet: there is no texture painting or texture slot |
| Transpose, Gizmo 3D | Not yet. Use the **Move** brush, or move the camera with the right-shelf buttons |
| UV Master | Not yet. UVs that come in with a GoZ model are kept (and saved in projects) |
| Zadd / Zsub | **Zadd** / **Zsub** on the top shelf; <kbd>Alt</kbd> flips for one stroke |
| ZPlugin | No in-app plugin system yet. Automate with the Python module, `sculptforge-cli` and MCP ([Scripting](scripting.md)) |
| ZRemesher | Not yet (quad retopology is on the roadmap) |
| ZScript | Script commands: the console (backtick key), script files, `sculptforge-cli`. See [Scripting](scripting.md) |
| ZTL, ZPR files | Not supported. Use GoZ, OBJ, FBX or GLB to bring models over; save work as `.sfproj` |

## SculptForge terms

| Term | Meaning |
|---|---|
| Brush Lab | The AI brush creator: describe a brush in words and it builds a recipe you can tweak. Opens from **AI Brush**. See [Brush Lab](brush-lab.md) |
| Brush code | One line of text (`sfbrush1:...`) that holds a whole brush, for sharing in chat or a forum. Paste with <kbd>Ctrl</kbd>+<kbd>V</kbd> |
| Brush library | Your saved custom brushes, loaded at every start |
| Brush recipe | The data that defines a custom brush (direction, depth, plane, pinch, pattern and so on): values, never code |
| `.sfbrush` | A file holding one brush recipe |
| `.sfproj` | A SculptForge project file: the whole scene. See [Projects](projects.md) |
| Command palette | <kbd>F3</kbd>: search every command, brush and setting by name and run it |
| Community gallery | Shared CC0 brushes you can install in one click (**Community brushes**) |
| Console | The script console (backtick key): every action appears as a script line |
| Ctrl help | Hold <kbd>Ctrl</kbd> and point at a control to see what it does, its shortcut and its command |
| Gaussian splat | A 3D capture made of soft coloured blobs (from phone scanning apps). Load one as a reference, convert it to a mesh, or turn a sculpt into splats. See [Gaussian splats](splats.md) |
| Generate 3D | Turn an image or a description into a sculptable model. See [Image and text to 3D](generate-3d.md) |
| LOD chain | A series of lighter copies of a model (LOD0, LOD1...) for game engines. See [Geometry](geometry.md#lod-chains) |
| MCP | Model Context Protocol: `sculptforge-cli --mcp` lets AI agents such as Claude run SculptForge commands |
| Send to Unreal | Export and import a model into a running Unreal Editor in one step (<kbd>Ctrl</kbd>+<kbd>U</kbd>) |
| Shelves, palettes, trays | The interface around the canvas: shelves hold the most-used controls, palettes hold everything, trays hold pinned palettes. See [The interface](interface.md) |
| Studio brush folder | A shared, read-only brush folder for a team (`SCULPTFORGE_BRUSH_PATH`). See [For studios](studios.md) |

## See also

- [Welcome to SculptForge](welcome.md)
- [The interface](interface.md)
- [Keyboard and mouse reference](shortcuts.md)
