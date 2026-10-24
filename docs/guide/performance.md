# Performance and big meshes

SculptForge is built to sculpt tens of millions of polygons. Brushes, normal updates and the picking structure
run on every core of your processor, a stroke sends the graphics card only the part of the mesh it changed, and
undo stores only the vertices a stroke touched, so it stays quick however big the model is. This chapter
explains what to expect, the settings in **Preferences > Performance**, and how to get the most out of a big
sculpt.

![Preferences > Performance after a stroke: antialiasing, Fast Navigation, multithreading, what the stroke cost per dab, and memory](images/performance-panel.jpg)

## What to expect

On a 2021 laptop (Apple M1 Pro, 10 cores, integrated GPU), with X symmetry on and a medium Draw Size:

| Mesh | One dab of a medium brush | Turning the view |
|---|---|---|
| 4 million triangles | about 1.5 ms | about 200 frames a second |
| 16 million triangles | about 6 ms | about 85 frames a second |
| 33 million triangles | about 10 ms | about 50 (4x antialiasing), 70 with Fast Navigation |

A dab is one step of the brush along the stroke; the app draws several per frame when you move fast. Under
16 ms per dab a stroke keeps up with a 60 Hz display. Very large brushes cost more, since they move more
vertices: a brush covering a tenth of a 33 million triangle sphere (over a million vertices per dab) takes about
90 ms per dab.

[Performance figures](https://github.com/islandmed/SculptForge/blob/main/docs/performance.md) has the full
measurements, and how to run the same benchmark on your own machine.

## Preferences > Performance

Open **Preferences** and click **Performance**.

| Control | What it does |
|---|---|
| **Antialiasing** (Off, 2x, 4x, 8x) | How many samples per pixel the 3D view uses to smooth edges. 4x is the default. More samples look smoother and cost more on huge meshes. If the graphics card can't make room for them, the view quietly uses fewer and the panel says so |
| **Fast Navigation** | While you rotate, pan or zoom a scene of 10 million triangles or more, draw it without antialiasing so the view keeps up. It is smooth again as soon as the view stops. On by default |
| **Multithreading** | Run brushes, normal and picking updates and project saving on every core (the number of threads is shown beside it). The results are exactly the same with it off; turn it off only to compare speeds or rule it out when something goes wrong |
| **Last stroke** | What your last stroke cost per dab: **Brush** (finding and moving the vertices), **Normals** (shading), **BVH** (the structure that finds the surface under the cursor), and the **GPU upload** per frame |
| **Memory** | What the scene holds: the meshes, the BVHs, the subdivision levels kept for stepping between them, the undo history (up to 1 GB; the oldest steps are dropped beyond that) and, roughly, the graphics card's buffers |

Like the other preferences, these last until you quit; **Preferences > Config > Store Config** keeps them. Every
setting is also in the <kbd>F3</kbd> command palette (type "antialiasing", "fast navigation" or
"multithreading").

## Working with big meshes

- **Sculpt at the lowest level that holds the detail.** With [subdivision levels](subdivision-levels.md) you can
  shape the forms a few levels down and only go to the top for fine detail. Each level down is four times
  lighter.
- **Hide what you're not working on.** Hidden SubTools aren't drawn and hidden parts of a SubTool aren't
  sculpted (**Tool > Visibility**). A hidden SubTool costs no drawing time at all.
- **Memory.** A sculpting mesh takes about 60 bytes per triangle, its BVH about 20, and its subdivision levels
  about 75 more, so a 33 million triangle sculpt with levels needs about 5 GB of memory, and opening or saving a
  project briefly needs about twice that. Project files over 4 GB are saved in the ZIP64 format.
- **The graphics card.** A mesh needs about 25 bytes per triangle on the graphics card. If a SubTool doesn't fit,
  SculptForge doesn't draw it and says so in the status bar instead of failing: hide other SubTools or step down
  a subdivision level.
- **Undo** keeps only what each stroke changed, so a stroke on a 33 million triangle mesh undoes in a few tens of
  milliseconds, and the history holds many such strokes within its 1 GB.

## Measuring your own machine

`sculptforge --bench` builds a sphere at 4, 16 and 32 million triangles and measures every brush, undo,
stepping subdivision levels, saving and opening a project, and turning the view, then prints a report.
`sculptforge-cli --bench` measures the same without the graphics card. Options:

| Option | What it does |
|---|---|
| `--bench-sizes 4,16,32` | The sizes, in millions of triangles |
| `--bench-dabs 40` | Dabs per test stroke |
| `--bench-brushes large` | Only the brushes whose name contains the text (here the large-radius ones) |
| `--bench-no-project` | Skip saving and opening the project (which needs twice the memory) |
| `--bench-out report.md` | Write the report to a file |

The `perf_info` command (also in the [Console](scripting.md)) prints the threads in use and the memory the
scene holds; `set_multithreading enabled=false` turns multithreading off from a script.

## See also

- [Subdivision levels](subdivision-levels.md)
- [Troubleshooting and FAQ](troubleshooting.md)
- [Glossary](glossary.md)
