# What's new

What changed in each version of SculptForge, newest first, with links to the chapters that explain it. This
chapter opens by itself the first time you start a new version; you can always find it again under
**Help > What's New**. Every detail is in the
[changelog](https://github.com/islandmed/SculptForge/blob/main/CHANGELOG.md).

## Next version (in development)

### Subdivision levels

Sculpt one model at several resolutions, as with ZBrush's SDiv: big forms low, fine detail high, and back down
without losing anything.

- **Divide** (<kbd>Ctrl</kbd>+<kbd>D</kbd>) adds a level and keeps the ones below. Quads stay quads
  (Catmull-Clark); **Smt** off divides without smoothing.
- **Lower Res** / **Higher Res** (<kbd>Shift</kbd>+<kbd>D</kbd> / <kbd>D</kbd>) and the **SDiv** slider step
  between levels. Detail sculpted high rides along when you reshape low, and edits high show up softened below.
  Stepping back and forth is instant, even at millions of polygons.
- Mask, polypaint, paint layers, polygroups, hidden faces, UVs and sculpt layers follow the levels.
- **Del Lower**, **Del Higher** and **Reconstruct Subdiv** (find the lower levels of a model subdivided
  elsewhere). Undo walks back through strokes and level changes; projects save every level; the SubTool list
  shows "SDiv 3/5".
- <kbd>Ctrl</kbd>+<kbd>D</kbd> now divides with levels; the old one-way **Subdivide** is still in
  **Tool > Geometry**.

See [Subdivision levels](subdivision-levels.md).

### Gizmo 3D: move, scale, rotate and pose

ZBrush's Gizmo 3D, with the same keys: <kbd>W</kbd> Move, <kbd>E</kbd> Scale, <kbd>R</kbd> Rotate, <kbd>Q</kbd>
back to Draw (also on the top shelf and in the **Transform** palette).

- **Handles over the model.** Arrows, plane squares and a free-move centre; axis rings and a view ring; axis
  boxes and a uniform centre. <kbd>Shift</kbd> snaps to 15 degrees, the grid or 10% steps.
- **Only the unmasked part moves**, so posing is: mask the body, <kbd>Alt</kbd>+click the shoulder to put the
  pivot there, raise **Falloff** for a soft joint, turn the arm. With X symmetry both arms move together.
- **Pivot tools**: <kbd>Alt</kbd>+drag the gizmo to move it alone, **Reset**, **To Surface**, World or Surface
  axes.
- **Numeric entry** in the Gizmo 3D panel, **All SubTools** (Transpose All), one undo step per drag, sculpt
  layers move with the surface, and the `transform` command for scripts.
- Fast on big models: a drag frame on 1.3 million triangles takes about 10 ms.

<kbd>W</kbd> no longer toggles the wireframe: use <kbd>Shift</kbd>+<kbd>F</kbd> (as in ZBrush) or **PolyF** on the
right shelf. See [Moving, rotating and posing](gizmo.md).

## Version 0.4.2

### Texture painting on UV maps

Paint into a texture instead of the vertices, so the colour detail comes from the texture (1K to 8K), not from
the mesh: sharp decals, pores and freckles on a light game mesh.

- **Auto UV.** **Tool > Texture Map > Auto UV** unwraps any sculpt in the background. Models that come in with
  UVs keep them, and now OBJ, FBX, USD and glTF carry UVs in and out too.
- **New Texture.** Pick a size and click **New Texture**: the Paint brush, **Rgb** sculpting, fills, cavity paint
  and colour picking (<kbd>C</kbd>) now work on the texture, with the mask, symmetry, pen pressure and alphas.
  Strokes never paint through to the far side, and seams stay invisible.
- **Vertex or Texture.** A toggle on the top shelf chooses where paint goes for each SubTool.
- **The same paint layers.** **Tool > Paint Layers** works on the texture's layers: blend modes, opacity,
  merge, flatten, all undoable.
- **Convert both ways**, **Show UVs**, **Import / Export Texture** (PNG). Projects save textures and their
  layers, and model exports write the texture next to the model (OBJ `.mtl`, glTF, USD, FBX, GoZ).

See [Texture painting](texture-painting.md).

## Version 0.4.1

A fix for graphics tablets: the brush ring no longer flickers between two places on the model (it looked like
a second brush blinking, most often while masking or smoothing). Some tablet drivers, Windows Ink among them,
send extra mouse movements beside the pen's; now, while the pen is in range, only the pen moves the brush. See
[Pen tablets](troubleshooting.md#pen-tablets) if your pen still misbehaves.

## Version 0.4

Version 0.4 is about making SculptForge feel like home to ZBrush artists. It was shaped by feedback from a
working ZBrush modeler and by what sculptors say on the ZBrush and Mudbox forums.

### Navigation that works like ZBrush

- **Free rotation.** Dragging turns the model the way the cursor moves and can roll it right over the top,
  as in ZBrush. If you prefer the model to stay upright, set **Preferences > Interface > Rotation** to
  **Turntable**.
- **Snap to a view.** Hold <kbd>Shift</kbd> while rotating to snap to the nearest front, back, side, top or
  bottom view.
- **ZBrush zoom.** <kbd>Alt</kbd>+drag on the background pans; let go of <kbd>Alt</kbd> while still dragging
  and the rest of the drag zooms. With the right mouse button, <kbd>Alt</kbd> pans and <kbd>Ctrl</kbd> zooms.

See [Navigating the view](navigation.md).

### Mask and isolate with a lasso or rectangle

- **MaskLasso and MaskRect.** Pick the shape at the top of **Tool > Masking**, then <kbd>Ctrl</kbd>+drag to
  mask everything inside it; add <kbd>Alt</kbd> to unmask. The drag can start on the model or beside it.
- **SelectLasso.** In **Tool > Visibility**, pick **SelectLasso** next to SelectRect, then
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag a free-hand shape to keep only what's inside (add <kbd>Alt</kbd> to
  hide it instead).

See [Mask with a lasso or rectangle](masking.md#mask-with-a-lasso-or-rectangle) and
[Polygroups and visibility](polygroups.md).

### A material made for sculpting

The new **Sculpt** material is the default: plain clay under a strong key light, with crisp highlights and
deep shading in the creases, so even small strokes stay easy to read. The other materials are still in the
material picker (<kbd>M</kbd>), and projects keep the material they were saved with. See
[Polypaint and materials](polypaint.md).

### Quads stay quads

Models you bring in from ZBrush (GoZ) or as OBJ, FBX, PLY or USD keep their quads: **PolyF** shows them as
quads, and exporting writes them back as quads. Subdivide turns each quad into four, voxel remesh now gives
an even grid of quads (like DynaMesh), and new cubes and planes are made of quads. See
[Quads and triangles](geometry.md#quads-and-triangles).

### Paint layers

Polypaint now has layers, like Photoshop: each SubTool can have a stack of colour layers with their own
opacity, visibility, lock and blend mode (Multiply, Screen, Overlay, Soft Light, Add and more). Painting on a
layer never destroys what's underneath, and you can merge, flatten, duplicate and reorder layers. ZBrush has
never had this. Find it in **Tool > Paint Layers**, and see [Paint layers](polypaint.md#paint-layers).

![The Paint Layers sub-palette with three layers and their blend modes](images/paint-layers.jpg)

### Easier to install

The [download links](installing.md) now always get the newest version, and there are one-command installers
for Windows, macOS and Linux.

## Version 0.3

- **SubTools**: several separate meshes in one scene. Append, duplicate, split, merge, solo, and
  <kbd>Alt</kbd>+click to select. See [SubTools](subtools.md).
- **A ZBrush-style interface**: shelves around the canvas, alphabetical palettes, the LightBox
  (<kbd>,</kbd>), the brush, stroke, alpha and material pickers, hold-<kbd>Ctrl</kbd> help on every control
  and a command palette (<kbd>F3</kbd>). See [The interface](interface.md).
- **Graphics tablets**: pen pressure, the eraser end and pen buttons. See [Sculpting and brushes](sculpting.md).
- **A brush library** with brush codes to share and a community gallery. See
  [Brush Lab and sharing brushes](brush-lab.md).
- **ComfyUI** workflows for image and text to 3D. See [Image and text to 3D](generate-3d.md).
- **This user guide**, built into the app (**Help > User Guide**, <kbd>Shift</kbd>+<kbd>F1</kbd>).

## Version 0.2

- [Masking](masking.md), [polygroups and visibility](polygroups.md), and
  [polypaint with MatCaps](polypaint.md).
- New brushes: DamStandard, ClayBuildup, SnakeHook, Trim, Layer and Nudge, with strokes and alphas.
- [Project files](projects.md) with autosave, recent files and QuickSave, and
  [GoZ exchange with ZBrush](exporting.md).

## Version 0.1

The first release: sculpting with symmetry and sculpt layers, voxel remesh and decimation, game-ready LOD
chains and Send to Unreal, image and text to 3D with free AI models, Gaussian splats, OpenUSD, and scripting
from the command line, Python and AI agents.
