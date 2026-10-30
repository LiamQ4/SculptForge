# CRKW Auto Retopo + Project

Select the sculpted SubTool at the resolution you want to preserve, then choose
**Script > Auto Retopo + Project**. The same operation can be run with
**Script > Run script file...** and `scripts/crkw_auto_retopo.sfscript`.
The installed copy is in `share/sculptforge/scripts`.

The operation freezes the displayed source surface, creates a lower quad base
with QRemesh, then subdivides to the level closest to the source's point count.
It projects geometry and polypaint from that source at the base and each higher
level. Keep Groups is enabled by default: source polygroup IDs transfer to the
new base and propagate through subdivision; remeshing follows their boundaries.
Borders can shift with the new topology. Use `keep_groups=false` to disable it.
Lower subdivision levels remain editable. Existing painted textures are
transferred to new UVs on the base before subdivision.

The original SubTool is retained and hidden; its geometry, layers, levels, masks
and live undo history are kept. The result is a separate selected SubTool named
`<original> Retopo`. Select the original to return to it. No scene meshes are
changed until the result is ready. The stored reference is the current displayed
state, not an arbitrary earlier point on an undo timeline. This runs synchronously
through the existing Script system; allow the operation to finish.

The automatic base budget is source points / 16, limited to 16-2500 quads.
Subdivision counts change in discrete steps, so the final count is approximate.
For a chosen quad budget or projection range, run this in the console or edit
the script:

```text
crkw_retopo_project target=2500 distance=0.05 max_points=8000000 keep_texture=true keep_groups=true
```

`distance` is a fraction of the bounding-box diagonal. The status reports final
projection hits and misses; a missed vertex keeps its subdivided position.
Smaller ranges reduce projection onto nearby opposing surfaces. Only the frozen
source is sampled, even when other SubTools are visible. Masks and hidden faces
do not exclude parts of the source from this whole-SubTool workflow.

Remeshing changes topology. A MetaHuman template's original remains available
for conform export; the newly retopologized mesh is a general sculpt mesh.
