# CRKW MetaHuman Sculpt

Open **CrokedWillo > MetaHuman Sculpt**. These are native SculptForge tools;
Blender does not need to be running.

## Templates and sculpting

The assets folder contains `FullBody.fbx`, `head.fbx`, `body.fbx` and the
`T_Gray_*` PNG maps from your local MetaHuman Sculpt installation. The development
build defaults to the adjacent `MetaHuman Sculpt/metahuman_sculpt/assets` folder.
Templates and maps are external assets, not distributed in this repository or
portable app. Use assets you are entitled to use.

Choose **Full Body**, **Head + Body**, or **Head Only**, then **Create MetaHuman**.
Creation appends native SubTools and keeps existing scene objects. **Start
Sculpting** selects the Standard brush and disables Dyntopo. Sculpt proportions
while preserving indexed base topology. Avoid remesh, vertex reordering, and
deleting or applying subdivision levels that replace the base.

Each character stores its topology fingerprint, original primary UVs and part
identity in the `.sfproj` project. **Fix UVs** restores that character's original
primary UVs without changing sculpted positions. Return every character part to
subdivision level 1 first. UV repair supports undo and rejects changed topology;
it cannot reconstruct a remeshed template.

## Guide preview

**Show UV Textures** offers Base Color, UV Guides and Annotated Guides. Body
annotation uses the available body guide map. Preview affects MetaHuman SubTools
in the scene and uses separate display textures/UVs; turning it off reveals the
original painted texture. Full Body uses a display atlas for body and head maps.
Real UVs and exported textures remain unchanged. Display maps are limited to
2048 pixels per part. Projects remember the preview mode; external maps must
remain accessible to restore it.

In **Tool > Texture Map**, hover the texture box and press **Ctrl+V** to paste
an image from the clipboard (or a copied image-file path). Click the box to
browse for an image. This replaces the selected SubTool's paint texture and
layers, using its existing UVs; **Ctrl+Z** restores the previous texture. Pasting
or importing turns off that SubTool's guide preview and enables texture display
so the result is visible immediately. Paste head and body maps onto their
matching SubTools. Guide previews remain available from MetaHuman Sculpt.

**Texture Manager** opens a compact floating panel from MetaHuman Sculpt,
CrokedWillo, or Tool > Texture Map. Each SubTool has a large preview card,
labelled **Guide** for a display map or **Texture** for an editable map. Hover a
card and press Ctrl+V, use **Paste**, or click **Swap image...** to replace that
SubTool's map. The target becomes selected, so Ctrl+Z restores its previous
texture independently of the other SubTools. Meshes without UVs need UVs before
importing. Previews include inactive and hidden SubTools.

## Export

Select a MetaHuman SubTool and choose **Export for MetaHuman**. The active
character is exported as separate `head.fbx`, `body.fbx` (when present),
`eyeLeft.fbx`, `eyeRight.fbx` and `teeth.fbx`. Full Body is split using its saved
body/head material partition. Matching filenames are replaced; unrelated files
are kept. The default is the ignored local `out/metahuman-export` folder.

Export validates all parts before writing and restores saved primary UVs on
copies. It preserves coincident template control points instead of welding
them. FBXs contain base geometry in centimeters. Higher subdivision fine detail
is excluded; sculpt layers must first be baked into the intended base shape.
Only the primary UV set is supported. Additional UV sets, DNA, rigging, skin
weights, painted texture maps and animation are not exported.

The intended Unreal workflow is From Template / Mesh Fit with Match Vertices by
UVs. An actual Unreal conform/rig round trip still needs verification; successful
FBX export alone does not establish compatibility with every MetaHuman version.

## Script commands

- `crkw_mh_create assets="path/to/assets" layout=FULL` (`SEPARATE` or `HEAD` also supported)
- `crkw_mh_fix_uvs`
- `crkw_mh_preview mode=GUIDES` (`BASE`, `ANNOTATED`, `OFF` also supported)
- `crkw_mh_export folder="path/to/export"`

`CRKW_METAHUMAN_ASSETS` supplies the folder when `assets` is omitted. Portable
tests generate synthetic assets and do not redistribute MetaHuman templates.

## Local validation

The Windows Release build passed 43,460 core checks, including 65 MetaHuman
checks, and the native panel test. All three layouts were also exercised with
the local templates: guide display, full-body splitting, subdivision base export,
UV repair, and project save/reopen. The installed portable app passed its panel
test. The full CTest suite passed 46/48 tests; the existing `ui_lightbox`
QuickSave help-text and `ui_zbrush_feel` negative Focal Shift failures remain.
Logs and local asset validation output are under ignored `out/`.

CRKW installs to `out/crkw-app` only (see CKW_WORKFLOW.md); since 2026-10-26
`out/crkw-metahuman-app` is a junction to it, so older shortcuts and taskbar pins
open the same latest build.
