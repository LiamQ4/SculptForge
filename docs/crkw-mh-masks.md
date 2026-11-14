# CRKW MH Mask Manager

Click **Masks** on the right shelf, directly below Camera. It toggles the manager
without opening another panel. You can also use **MetaHuman Sculpt > Mask Manager**
or **CrokedWillo > MH Mask Manager**.
Select a native MetaHuman head or full-body Part. The nine presets match
`mh_part_sculpt/masks.py`: UV ellipse seeds, polygon centres, edge-ring growth,
and smoothstep feathering. Quad diagonals do not count as mesh edges.

Click a region to toggle it. Ctrl+click isolates that region. Active buttons
are highlighted; enabled regions form one combined free area. Disabling the
last region clears the mask. Both / R / L selects the UV core for paired
regions; Both enables X symmetry and R/L disables it. Central regions retain
their central core, matching the Blender add-on. The UI identifies R/L by the
character's side, not the viewer's side.

Rings and Feather adjust the most recently selected region and rebuild the
active set. Isolate, Add, Protect, Invert and Clear are available explicitly.
Frame centres the camera on vertices whose mask is below 0.5.

Save custom region stores the vertices with mask < 0.5 as `mhr_<Name>` and adds
a button. Saved regions and settings persist in `.sfproj` projects. Custom
seeds are tied to the topology/subdivision level on which they were saved;
return to that level or save again if topology changes. Presets can be used
on other subdivision levels with their current edge-ring density.

All operations use native SculptForge mask undo. Undo/redo restores the mask
and region-toggle state together. Vertex positions, topology and UVs remain
unchanged. The full-body head partition comes from the template's immutable
part reference, rather than editable face groups. Keep the primary MetaHuman
UV layout for guide-based presets. Manual brush-mask edits are replaced by
the active-region combination when a region or its settings changes.


## Source maintenance — 2026-11-14

Reviewed for upstream v0.12.16. CRKW uses the upstream Parts/PartData model; existing `crkw_metahuman` and `crkw_mask_manager` project payloads retain their format. CRKW material indices remain stable. Current build, installation and validation status is recorded in [the CRKW workflow](../CKW_WORKFLOW.md). Earlier validation counts above are historical.
