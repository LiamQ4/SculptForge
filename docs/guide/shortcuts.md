# Keyboard and mouse

This chapter is the reference for every default keyboard shortcut and mouse gesture in SculptForge. Inside the
app, <kbd>F1</kbd> opens the same list as the Controls window, showing your own keys if you've changed any.

## Changing shortcuts

Every key below can be reassigned: hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> and click a button, then press the new
key, or use **Preferences > Hotkeys**. Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> (Store Config) to
keep your changes. See [Customizing the interface](interface.md#customizing-the-interface).

On macOS, <kbd>Cmd</kbd> works wherever a shortcut says <kbd>Ctrl</kbd>. Single-letter shortcuts don't fire
while you type in a text field.

## Keyboard shortcuts

The table lists the default shortcut of every action, grouped by palette. It is generated from SculptForge's
action registry, the same source as the Controls window.

<!-- BEGIN GENERATED: shortcuts -->

### Brush

| Action | Keys | What it does |
|---|---|---|
| Brush picker | <kbd>B</kbd> | Pop up every brush under the cursor. Type the first letter of a brush, then the orange letter on its tile (B S T = Standard). |
| Clay | <kbd>2</kbd> | Builds up clay that fills the hollows first. |
| ClayBuildup | <kbd>0</kbd> | Square, flat-topped clay strokes for blocking in forms. |
| Crease | <kbd>6</kbd> | Cuts a sharp crease; Alt raises a ridge. |
| DamStandard | <kbd>9</kbd> | A sharp V-shaped crease with the sides pinched in (ZBrush DamStandard). |
| Flatten | <kbd>7</kbd> | Presses the surface toward a plane. |
| Inflate | <kbd>5</kbd> | Swells the surface outward along its normals. |
| Move | <kbd>4</kbd> | Grabs the surface under the cursor and pulls it along. |
| Paste brush | <kbd>Ctrl</kbd>+<kbd>V</kbd> | Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file. |
| Pinch | <kbd>8</kbd> | Pulls the surface toward the stroke to tighten edges. |
| Smooth | <kbd>3</kbd> | Relaxes the surface. Hold Shift to smooth with any brush. |
| Standard | <kbd>1</kbd> | Raises the surface along the stroke; Alt digs in. |

### Color

| Action | Keys | What it does |
|---|---|---|
| Pick colour | <kbd>C</kbd> | Take the paint colour from the model under the cursor. |

### Draw

| Action | Keys | What it does |
|---|---|---|
| Draw Size at cursor | <kbd>S</kbd> | A Draw Size slider under the cursor: drag it, or type a number and press Enter. |
| Larger brush | <kbd>]</kbd> | Make the brush 10% larger (hold to repeat). |
| Paint | <kbd>P</kbd> | Paint colour without sculpting (ZBrush Rgb on, Zadd off). Alt paints back to white, Shift still smooths. |
| Smaller brush | <kbd>[</kbd> | Make the brush 10% smaller (hold to repeat). |
| Strength at cursor | <kbd>I</kbd> | A Strength slider under the cursor: drag it, or type a number and press Enter. |

### Edit

| Action | Keys | What it does |
|---|---|---|
| Redo | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> or <kbd>Ctrl</kbd>+<kbd>Y</kbd> | Redo the last undone action. |
| Undo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Undo the last action. |

### File

| Action | Keys | What it does |
|---|---|---|
| Export GLB... | <kbd>Ctrl</kbd>+<kbd>E</kbd> | Export the model as glTF binary, with sculpt layers as blend shapes (morph targets). |
| New Sphere | <kbd>Ctrl</kbd>+<kbd>N</kbd> | Replace the scene with a new sphere ready for sculpting. |
| Open... | <kbd>Ctrl</kbd>+<kbd>O</kbd> | Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats. |
| Quit | <kbd>Ctrl</kbd>+<kbd>Q</kbd> | Close SculptForge (asks to save changes first). |
| Save | <kbd>Ctrl</kbd>+<kbd>S</kbd> | Save the project (.sfproj: every SubTool, layer, brush and setting). Asks for a name the first time. |
| Save As... | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> | Save the project under a new name. |
| Send to Unreal | <kbd>Ctrl</kbd>+<kbd>U</kbd> | Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution). |

### Help

| Action | Keys | What it does |
|---|---|---|
| Command palette | <kbd>F3</kbd> or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> | Search every command, brush and setting by name and run it. Commands that need parameters open in the console. |
| Controls | <kbd>F1</kbd> | Every shortcut and mouse gesture, generated from the commands themselves. |
| User Guide | <kbd>Shift</kbd>+<kbd>F1</kbd> | The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open the chapter about it. |

### LightBox

| Action | Keys | What it does |
|---|---|---|
| LightBox | <kbd>,</kbd> | Show or hide the LightBox: thumbnails of your projects, models, brushes, alphas, materials, images, splats and QuickSaves along the top of the canvas. Double-click to load. |

### Material

| Action | Keys | What it does |
|---|---|---|
| Material picker | <kbd>M</kbd> | Pop up the materials under the cursor. Hover one to preview it on the model; click to keep it. |

### Preferences

| Action | Keys | What it does |
|---|---|---|
| Load UI... | <kbd>Ctrl</kbd>+<kbd>L</kbd> | Load a layout file someone shared (or you saved with Save UI). |
| Shelves | <kbd>Tab</kbd> | Hide or show the shelves and trays around the canvas. |
| Store Config | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> | Keep the current layout, shortcuts and interface preferences for future sessions. Like ZBrush, SculptForge never saves them on its own. |

### Render

| Action | Keys | What it does |
|---|---|---|
| Fullscreen | <kbd>F11</kbd> | Fill the screen with SculptForge (again: back to a window). |
| Wireframe | <kbd>Shift</kbd>+<kbd>F</kbd> | Show the mesh's polygons (quads and triangles) over the surface (ZBrush PolyFrame). |

### Script

| Action | Keys | What it does |
|---|---|---|
| Console | <kbd>`</kbd> | The script console: every command you run appears as a script line, and you can type commands yourself. |

### Tool

| Action | Keys | What it does |
|---|---|---|
| Subdivide | <kbd>Ctrl</kbd>+<kbd>D</kbd> | Smoothly subdivide the mesh (Loop subdivision). Each level multiplies the triangle count by 4. |

### Transform

| Action | Keys | What it does |
|---|---|---|
| Draw | <kbd>Q</kbd> | Draw mode: brushes sculpt and paint (leaves the Gizmo 3D). |
| Frame | <kbd>F</kbd> | Fit the whole model in the view. |
| Move | <kbd>W</kbd> | Gizmo 3D, Move: drag an arrow to move along an axis, a square to move in a plane, the centre to move in the view. Masked parts stay; Shift snaps to the grid; Alt+drag moves only the gizmo. W again: back to Draw. |
| Rotate | <kbd>R</kbd> | Gizmo 3D, Rotate: drag a ring to turn about that axis, the outer ring to turn in the view. Mask the body and put the pivot at a joint to pose a limb. Shift snaps to 15 degrees. R again: back to Draw. |
| Scale | <kbd>E</kbd> | Gizmo 3D, Scale: drag a box to stretch along an axis or the centre to scale evenly (right or up: bigger). Shift snaps to 10% steps. E again: back to Draw. |
| X | <kbd>X</kbd> | Mirror strokes across X (left and right). |

<!-- END GENERATED: shortcuts -->

## Mouse and pen gestures

| Action | Gesture |
|---|---|
| Sculpt | Left-drag on the model |
| Invert the brush (subtract) | Hold <kbd>Alt</kbd> while sculpting |
| Smooth | Hold <kbd>Shift</kbd> while sculpting (works with any brush, and with Paint) |
| Rotate | Left-drag on the background, or right-drag; hold <kbd>Shift</kbd> to snap to an axis view |
| Pan | <kbd>Alt</kbd>+left-drag on the background, <kbd>Alt</kbd>+right-drag, or middle-drag |
| Zoom | Mouse wheel; <kbd>Alt</kbd>+drag on the background then release <kbd>Alt</kbd>; <kbd>Ctrl</kbd>+right-drag; <kbd>Ctrl</kbd>+middle-drag; or <kbd>Ctrl</kbd>+left-drag on the background (when nothing is masked) |
| Paint the mask | Hold <kbd>Ctrl</kbd> while stroking on the model |
| Erase the mask | Hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> while stroking on the model |
| Invert the mask | <kbd>Ctrl</kbd>+click the background |
| Clear the mask | <kbd>Ctrl</kbd>+drag on the background (with **MaskPen**) |
| Mask inside a lasso or rectangle | <kbd>Ctrl</kbd>+drag with **MaskLasso** or **MaskRect** picked in **Tool > Masking** (add <kbd>Alt</kbd>: unmask) |
| Isolate a polygroup | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click it (again: invert the visibility) |
| Hide a polygroup | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+click it |
| Show everything | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click the background |
| Hide outside a rectangle or lasso | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag (add <kbd>Alt</kbd>: hide inside); **SelectRect** or **SelectLasso** in **Tool > Visibility** |
| Select a SubTool | <kbd>Alt</kbd>+click it in the view |
| Pan, zoom or orbit with a button | Drag on **Move**, **Zoom** or **Rotate** on the right shelf |
| Help on any control | Hold <kbd>Ctrl</kbd> and point at it (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>: scripting parameters) |
| Assign a shortcut | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click any button, then press the key |
| Move or remove shelf items | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+drag (with **Enable Customize** on) |
| Tablet pressure | Used automatically when a pen is detected (**Pressure > Strength** / **Pressure > Size**) |
| Open files | Drop a model (`.obj`, `.glb` ...), project, brush (`.sfbrush`), splat capture or image onto the window |

## Keys inside pop-ups and panels

Some keys only mean something while a pop-up or panel has the keyboard.

### Brush picker (B)

| Key | Action |
|---|---|
| A letter | Keep the brushes whose names start with the letters typed so far |
| The orange letter on a tile | Pick that brush (<kbd>B</kbd> <kbd>S</kbd> <kbd>T</kbd> = Standard) |
| <kbd>Backspace</kbd> | Take back one letter |
| <kbd>Enter</kbd> | Pick the first match |
| <kbd>Esc</kbd> | Close |

### Draw Size and Strength at the cursor (S, I)

| Key | Action |
|---|---|
| Digits and <kbd>.</kbd> | Type a value |
| <kbd>Enter</kbd> | Apply the typed value and close |
| <kbd>Esc</kbd> | Close without applying a typed value |

### Command palette (F3)

| Key | Action |
|---|---|
| <kbd>Up</kbd> / <kbd>Down</kbd>, <kbd>Page Up</kbd> / <kbd>Page Down</kbd> | Choose a row |
| <kbd>Enter</kbd> | Run it |
| <kbd>Shift</kbd>+<kbd>Enter</kbd> | Open a command with parameters in the console to edit them |
| <kbd>Esc</kbd> | Close |

### Console (`)

| Key | Action |
|---|---|
| <kbd>Enter</kbd> | Run the line (statements separated by `;`) |
| <kbd>Tab</kbd> | Complete a command or parameter name; press again to list the candidates |
| <kbd>Up</kbd> / <kbd>Down</kbd> | Walk through earlier lines (the last 200 are remembered between sessions) |

### Shortcut capture (Ctrl+Alt+click)

| Key | Action |
|---|---|
| Any key or chord | Becomes the new shortcut (taken from another action if it had it) |
| <kbd>Backspace</kbd> or <kbd>Delete</kbd> | Remove the shortcut |
| <kbd>Esc</kbd> | Cancel |

## ZBrush keys that work differently

Most ZBrush shortcuts do the same thing in SculptForge. A few differ because the feature works differently or
doesn't exist yet:

| ZBrush | SculptForge |
|---|---|
| <kbd>D</kbd> / <kbd>Shift</kbd>+<kbd>D</kbd> (subdivision level up / down) | No subdivision levels yet; <kbd>Ctrl</kbd>+<kbd>D</kbd> subdivides once and can be undone |
| <kbd>Space</kbd> (QuickMenu) | No QuickMenu; use the left-shelf tiles, <kbd>B</kbd>, <kbd>M</kbd>, <kbd>S</kbd> and <kbd>I</kbd> |
| <kbd>O</kbd> (Focal Shift) | No focal shift setting |
| <kbd>Q</kbd> <kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd> (Draw, Move, Scale, Rotate gizmo) | No transform gizmo; <kbd>W</kbd> toggles the wireframe (PolyF) |
| <kbd>Ctrl</kbd>+<kbd>W</kbd> (group masked) | **Tool > Polygroups > From mask** |
| <kbd>Shift</kbd> (snap the view while rotating) | No view snapping yet |
| <kbd>H</kbd> (hide the ZScript window) | <kbd>`</kbd> toggles the console |

## See also

- [The interface](interface.md)
- [Navigating the view](navigation.md)
- [Masking](masking.md)
- [Polygroups and visibility](polygroups.md)
