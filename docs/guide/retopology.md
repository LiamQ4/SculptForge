# Quad retopology and detail projection

**QRemesh** rebuilds a sculpt as clean, all-quad topology whose edge loops follow the forms, like ZBrush's
ZRemesher. **ProjectAll** then moves a mesh onto the surface of other SubTools, so the detail of the original
sculpt comes back onto the new topology, like ZBrush's Project All. Together they turn a dense, messy sculpt
(a voxel remesh, a scan, an AI-generated model) into a light quad mesh you can divide, sculpt on, unwrap and
animate.

![A lion bust after QRemesh: 5,000 quads whose loops follow the mane, shown with PolyF](images/qremesh.jpg)

## The usual workflow

1. Select the sculpt and open **Tool > Geometry**. Under **QRemesh**, turn on **Keep Original**.
2. Set **Target Polygons** (for example 5000) and click **QRemesh**. It runs in the background: a progress bar
   replaces the button, and **Cancel** stops it. The result replaces the SubTool, and the sculpt as it was is
   kept as a hidden SubTool called "*name* Original" right below it.
3. Divide the new mesh two or three times (**Tool > Geometry > Subdivide**, <kbd>Ctrl</kbd>+<kbd>D</kbd>).
4. Show the original again (the eye in **Tool > SubTool**) and click **ProjectAll** under **Tool > SubTool >
   Project**. The divided quads snap onto the sculpt and take its polypaint.
5. Hide or delete the original. You now have the same sculpt on clean quads.

![Left: the retopology of a sculpted sphere. Right: divided twice and projected, the ridges and the crease are back](images/project-all.jpg)

Both steps are single undo steps (<kbd>Ctrl</kbd>+<kbd>Z</kbd>).

## QRemesh

**Tool > Geometry > QRemesh** works on the active SubTool.

| Control | What it does |
|---|---|
| **QRemesh** | Starts the retopology. The status line reports the result, e.g. "QRemesh: 4987 quads from 47214 triangles (100% quads) in 1.0 s" |
| **Target Polygons** | About how many quads to make (100 to 200,000 on the slider, more from scripts). The result is usually within 5% of it |
| **Half / Same / Double** | Set the target to half, the same as or twice the SubTool's current polygon count |
| **Adaptive Size** | 0 makes quads all about the same size. Higher values make smaller quads where the surface curves and bigger ones on flat areas, for the same total |
| **Keep Groups** | Edge loops run exactly along the polygroup borders, and the result keeps the polygroups |
| **Detect Edges** | Hard edges, where faces meet at more than 50 degrees, get an edge loop, so boxes and bevels stay crisp (on by default) |
| **Use Polypaint** | Paint where you want density: red areas get up to four times the quads, blue areas down to a quarter, white areas are unchanged |
| **Symmetry** | Remesh one half and mirror it, welded on the seam, for exactly symmetric topology. It mirrors across the plane through the origin on the axis chosen in the **Transform** palette (X if none) |
| **Keep Original** | Keep the SubTool as it was, hidden, below the new one, ready for ProjectAll. Off: the SubTool is replaced, as in ZBrush (undo brings it back) |

What happens to your data:

- The result is all quads (stored as pairs of triangles, so **PolyF** draws quads and OBJ, FBX, PLY, USD and GoZ
  export them as quads), closed wherever the sculpt is closed. Open edges of the sculpt stay open, along the
  same outline.
- Sculpt layers are baked into the surface: the result has no layers.
- Mask and polypaint are interpolated from the closest point of the sculpt (polypaint is blurred to the new
  resolution until you project it back onto a divided copy).
- Polygroups are kept with **Keep Groups**; otherwise the result is one polygroup. UVs and the texture are
  dropped: unwrap again with **Tool > Texture Map > Auto UV**.
- Hidden parts are remeshed too. Very small separate pieces (smaller than a quad or two) can disappear; raise
  the target or QRemesh them separately.

### Guiding the edge flow

- **Polygroups** are the strongest guide. Paint a group around an eye, the mouth or a muscle, turn on **Keep
  Groups**, and loops will circle it. See [Polygroups](polygroups.md).
- **Polypaint** sets the density with **Use Polypaint**: paint the face red and the back of the head blue to
  spend the quads where they matter.
- **Adaptive Size** around 0.5 gives detailed areas more quads without your help; use 0 for an even mesh, for
  example before more sculpting.

### How it works

QRemesh is a field-aligned method after *Instant Field-Aligned Meshes* (Jakob, Tarini, Panozzo and
Sorkine-Hornung, 2015). It smooths a cross field (the directions the quad edges should take) over the surface
from coarse to fine, places a grid of the wanted size along it, extracts the polygons where neighbouring grid
corners meet, splits every polygon into quads and relaxes them back onto the sculpt. It is fast: about 2 seconds
for a 1.3-million-triangle sculpt to 5,000 quads, and 6 seconds to 100,000 quads, on a 10-core desktop.

> **ZBrush:** There are no curve guides (ZRemesher Guides brush) and no Freeze Border / Freeze Groups yet.
> Keep Groups, Use Polypaint, Adaptive Size, Half / Same / Double and symmetry work as you know them. The mask
> doesn't steer QRemesh: use polypaint for density.

## ProjectAll

**Tool > SubTool > Project > ProjectAll** projects every other *visible* SubTool onto the active one. Each vertex
looks along its normal, both ways, and moves to the nearest surface it finds within **Dist**; if there is none
straight along the normal, it takes the closest point within **Dist**.

| Control | What it does |
|---|---|
| **ProjectAll** | Projects the visible SubTools onto the active one (one undo step) |
| **Dist** | How far to search, as a fraction of the SubTool's size (bounding-box diagonal). Default 0.02. Raise it if parts don't pick up the detail; lower it where two surfaces are close together (lips, eyelids, fingers) |
| **PA Blur** | Smooths the projected result this many times, to soften noise from the source |
| **Mean** | Where there is surface on both sides of a vertex, settle halfway between them instead of on the nearest |
| **Geometry** | Project the shape (into the active sculpt layer, if you have layers) |
| **Polypaint** | Project the polypaint (into the active paint layer) when a source has any |

- **The mask protects:** fully masked vertices don't move; partly masked ones move part of the way. Mask the
  areas you have already fixed and project again with a different **Dist**.
- Hidden faces of the source are ignored, and hidden parts of the target don't move.
- ProjectAll is fast enough to use on dense meshes: about 0.6 seconds to project 1.25 million vertices from a
  1.3-million-triangle sculpt.

> **Tip:** Project after each division, not only at the end: divide once, ProjectAll, divide again, ProjectAll.
> Each pass starts closer to the sculpt, so a small **Dist** is enough and thin parts don't jump to the wrong
> surface.

## Scripting

| Command | Options |
|---|---|
| `qremesh` | `target` (16 to 2,000,000, default 5000), `adaptive` (0-1, default 0.5), `keep_groups`, `detect_edges` (default true), `use_polypaint`, `symmetry` (`x`, `y` or `z`), `keep_original`, `smooth` (relaxation passes, default 6), `seed` |
| `project_all` | `distance` (0.001-1, default 0.02), `mean`, `blur` (0-20), `geometry` (default true), `polypaint` (default true), `source` (one SubTool's name instead of all visible ones) |

```sh
sculptforge-cli -c 'open path=scan.glb; qremesh target=8000 adaptive=0.3 keep_original=true; subdivide levels=2; subtool_visibility index=1 visible=true; project_all distance=0.02; export_fbx path=retopo.fbx'
```

From scripts `qremesh` runs straight away (it doesn't return until it's done); in the app the button runs it in
the background.

## See also

- [Subdivide, remesh, decimate and LODs](geometry.md): voxel remesh first to fuse pieces, subdivide after QRemesh
- [SubTools](subtools.md): show, hide and duplicate the original
- [Polygroups and visibility](polygroups.md): guides for Keep Groups
- [Texture painting](texture-painting.md): Auto UV on the new topology
