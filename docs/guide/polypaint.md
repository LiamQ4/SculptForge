# Polypaint and materials

Polypaint stores a colour on every vertex of the mesh, so you can paint a sculpt without UVs or textures, as in
ZBrush. This chapter covers the Paint brush, painting while you sculpt, fills and cavity paint, picking colours,
and the MatCap materials that change how the model is shaded in the viewport.

## How polypaint works

- Colour is stored per vertex. The detail you can paint is limited by how dense the mesh is: on a light mesh,
  colour edges look soft. [Subdivide](geometry.md#subdivide) or [remesh](geometry.md#voxel-remesh) at a higher
  resolution before fine painting.
- Painting respects the [mask](masking.md) (masked areas keep their colour) and skips
  [hidden faces](polygroups.md).
- Symmetry works for painting as it does for sculpting.
- Polypaint is per SubTool and is saved in projects. It is exported as vertex colours to PLY, glTF/GLB, OBJ, USD
  (`displayColor`) and GoZ. FBX export doesn't include it.
- A mesh starts without polypaint. The first paint stroke or fill adds it (white everywhere you haven't painted).

> **Not yet:** texture painting on UVs, layers of paint, and baking polypaint to a texture map. See the roadmap.

## Paint with the Paint brush

1. Press <kbd>P</kbd>, click **Paint** on the top shelf, or pick **Paint** on the bottom shelf.
2. Choose a colour (see below).
3. Stroke over the model. Draw Size, Strength, the stroke type and the alpha work as they do for sculpting.

While the Paint brush is active:

- <kbd>Alt</kbd> paints back towards white.
- <kbd>Shift</kbd> still smooths the shape, as in ZBrush.
- The surface isn't changed.

Press <kbd>P</kbd> again, or click **Sculpt** on the top shelf, to go back to sculpting.

![Polypaint on a sculpt with the Color palette open](images/polypaint.jpg)

> **ZBrush:** **Paint** is ZBrush's Rgb on with Zadd off. **Sculpt** with **Rgb** on is Rgb with Zadd: the brush
> sculpts and paints at once.

## Paint while sculpting

Turn on **Rgb** on the top shelf (or **RGB while sculpting** in the Color palette). Every sculpt brush then also
paints the current colour along its stroke. The Smooth brush and <kbd>Shift</kbd>-smoothing never paint.

## Choose a colour

- The **left shelf** has a colour picker (a square plus a hue strip), two swatches and a **Fill** button. Clicking
  the second swatch swaps it with the main colour, like ZBrush's Alternate.
- The **Color** palette has a larger hue wheel with a hex value, ten preset swatches (white, near black, red,
  orange, yellow, green, blue, purple, skin and brown) and the fill buttons.
- Press <kbd>C</kbd> with the cursor over the model to pick the colour under it (the mesh needs polypaint for this).

## Fill, cavity paint and clear

The Color palette has four buttons:

| Button | What it does |
|---|---|
| **Fill** | Fills the unmasked area with the colour (the whole mesh when nothing is masked). Same as **Fill** on the left shelf |
| **Fill masked** | Fills only the masked area. Available when the mesh has a mask |
| **Cavity** | Darkens the creases and cavities with a dark brown, respecting the mask. A quick way to bring out detail |
| **Clear** | Removes all polypaint from the active SubTool |

All of them are undoable. **Show polypaint** in the same palette (or **Paint** on the right shelf) turns the colours
off in the viewport without deleting them.

> **Tip:** To paint a base colour and then dirt in the creases: **Fill** with the base colour, then **Cavity**. For
> a different cavity colour or strength, use the command (below).

## Materials (MatCaps)

Materials change only how the model is shaded in the viewport. They aren't exported and don't change polypaint.

| Material | Look |
|---|---|
| Sculpt | The default, made for sculpting: neutral warm clay under a strong key light from the upper left, a crisp highlight and deep cavity shading, so small strokes and undulations stay readable |
| Red Wax | Glossy deep red wax, the classic sculpting look |
| Clay | Matte grey-brown clay |
| Skin | Warm, soft skin tones |
| Chrome | Mirror-like metal, good for checking surface flow |
| White | Neutral off-white for judging forms |
| Bronze | Polished bronze |
| Studio | Softer clay shading with studio lighting (the default in earlier versions). Its colour is **Base colour** in the Material palette |

To choose one:

- Press <kbd>M</kbd> for the material picker under the cursor, or click the **Material** tile on the left shelf.
  Point at a material to preview it on the model, click to keep it. <kbd>Esc</kbd> or clicking away puts the
  previous material back.
- Open the **Material** palette, or the **Material** tab of the LightBox.

![The material picker with the seven MatCaps and Studio](images/material-picker.jpg)

Polypaint multiplies the material, and cavity shading stays on with every material (strongest with Sculpt). The
material is saved with the project: projects saved with Studio open with Studio.

> **Tip:** **Sculpt** is the material to judge strokes with. The prettier ones (Skin, Red Wax) soften small
> detail; switch to them for presenting.

> **Not yet:** loading your own MatCap images, and physically based materials or BPR-style renders.

## Scripting

| Command | What it does |
|---|---|
| `set_color color=#c08060` | Set the paint colour: `rrggbb`, `#rrggbb` (quote it in scripts), `r,g,b` or a name such as `white`, `skin`, `bone` |
| `paint_fill area=unmasked` | Fill; `area` is `unmasked` (default), `masked` or `all`; `strength` 0-1 |
| `paint_from_cavity color=#202020 strength=0.7 smoothing=2` | Cavity paint (defaults: dark brown, 0.7, 2 blur passes) |
| `paint_clear` | Remove all polypaint |
| `set_material name=Chrome` | Viewport material: a MatCap name (`Sculpt` is the default) or `studio` (desktop app only) |

```text
set_color color="#d9b38c"; paint_fill; paint_from_cavity strength=0.5
```

## See also

- [Masking](masking.md): protect areas from painting
- [Sculpting and brushes](sculpting.md): Draw Size, Strength, strokes and alphas also apply to painting
- [Exporting](exporting.md): which formats carry vertex colours
