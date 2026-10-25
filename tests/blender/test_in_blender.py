# SculptForge - Blender bridge: the add-on inside a real Blender (4.2+), headless
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   blender -b --factory-startup -noaudio --python-exit-code 1 --python tests/blender/test_in_blender.py -- \
#       --cli build/sculptforge-cli
#
# Sends a small scene to SculptForge (sculptforge-cli stands in for the app: blender_receive, then
# blender_send), receives it back and checks that the objects are unchanged: positions, n-gons,
# transforms, materials, modifiers, UVs, colours, mask and face sets. Then a topology change (subdivide)
# and a new SubTool come back as a rebuilt mesh and a new object. CMake runs it when `blender` is found.
import os
import subprocess
import sys
import tempfile

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "addons", "blender"))

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLI = argv[argv.index("--cli") + 1] if "--cli" in argv else "sculptforge-cli"
ROOT = tempfile.mkdtemp(prefix="sf_blender_test_")
os.environ["SCULPTFORGE_BLENDER_EXCHANGE"] = ROOT

import addon_utils  # noqa: E402

# Enabled as an add-on (so it has its preferences) straight from the source folder on sys.path.
addon_utils.enable("sculptforge_bridge", default_set=True)
import sculptforge_bridge as bridge  # noqa: E402
from sculptforge_bridge import exchange  # noqa: E402

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def arr(collection, prop, width, dtype=np.float64):
    out = np.empty(len(collection) * width, dtype=dtype)
    collection.foreach_get(prop, out)
    return out.reshape(-1, width) if width > 1 else out


def snapshot(obj):
    me = obj.data
    s = {"co": arr(me.vertices, "co", 3), "matrix": np.array(obj.matrix_world),
         "polys": [tuple(p.vertices) for p in me.polygons], "materials": [m.name for m in me.materials],
         "modifiers": [(m.name, m.show_viewport) for m in obj.modifiers]}
    if me.uv_layers.active:
        s["uv"] = arr(me.uv_layers.active.data, "uv", 2)
    if me.color_attributes.active_color:
        s["color"] = arr(me.color_attributes.active_color.data, "color_srgb", 4)
    if me.attributes.get(".sculpt_face_set"):
        s["sets"] = arr(me.attributes[".sculpt_face_set"].data, "value", 1, np.int32)
    if me.attributes.get(".sculpt_mask"):
        s["mask"] = arr(me.attributes[".sculpt_mask"].data, "value", 1)
    return s


def sculptforge(commands):
    r = subprocess.run([CLI, "-c", "; ".join(c + ' folder="%s"' % ROOT if c.startswith("blender_") else c for c in commands)],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    check(r.returncode == 0, "sculptforge-cli: " + "; ".join(commands) + (" (%s)" % r.stderr.strip() if r.returncode else ""))


def build_scene():
    for obj in list(bpy.data.objects):  # (not read_factory_settings: it would disable the add-on)
        bpy.data.objects.remove(obj)
    mat = bpy.data.materials.new("Clay")
    # A cube (quads, UVs) with a live modifier, rotated and scaled.
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(1.0, 2.0, 0.5), rotation=(0.3, 0.2, 0.9))
    cube = bpy.context.active_object
    cube.name = "Box"
    cube.scale = (1.0, 2.0, 0.5)
    cube.data.materials.append(mat)
    cube.modifiers.new("Bevel", "BEVEL")
    me = cube.data
    colors = me.color_attributes.new("Paint", "FLOAT_COLOR", "POINT")
    rgba = np.random.default_rng(1).integers(0, 256, (len(me.vertices), 4)) / 255.0
    rgba[:, 3] = 1.0
    colors.data.foreach_set("color_srgb", rgba.astype(np.float32).ravel())
    me.color_attributes.active_color = colors
    sets = me.attributes.new(".sculpt_face_set", "INT", "FACE")
    sets.data.foreach_set("value", np.array([1, 1, 2, 2, 3, 3], dtype=np.int32))
    mask = me.attributes.new(".sculpt_mask", "FLOAT", "POINT")
    mask.data.foreach_set("value", (np.arange(len(me.vertices)) / 7.0).astype(np.float32))
    # An n-gon disc, mirrored.
    bpy.ops.mesh.primitive_circle_add(vertices=7, radius=1.0, fill_type="NGON", location=(-3.0, 0.0, 1.0))
    disc = bpy.context.active_object
    disc.name = "Disc ü"
    disc.scale = (-1.0, 1.0, 1.0)
    # Monkey: triangles and quads, with UVs.
    bpy.ops.mesh.primitive_monkey_add(location=(0.0, -3.0, 0.0))
    bpy.context.active_object.name = "Suzanne"
    bpy.ops.object.select_all(action="SELECT")
    return ["Box", "Disc ü", "Suzanne"]


def main():
    check(bridge.prefs() is not None, "add-on enabled with its preferences")
    # No real SculptForge here: never start one.
    exchange.find_sculptforge = lambda root, override="": ""
    names = build_scene()
    before = {n: snapshot(bpy.data.objects[n]) for n in names}

    check(bpy.ops.sculptforge.send() == {"FINISHED"}, "Send to SculptForge")
    manifest = exchange.read_manifest(exchange.outbox(ROOT))
    check([o["name"] for o in manifest["objects"]] == names, "manifest lists the selected objects by name")

    # SculptForge loads them and sends them straight back: everything must be as it was.
    sculptforge(["blender_receive", "blender_send"])
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "Receive from SculptForge: " + bridge.state.message)
    check("3 updated" in bridge.state.message, "all three updated in place")
    check(sorted(o.name for o in bpy.data.objects) == sorted(names), "no objects added")
    for n in names:
        a, b = before[n], snapshot(bpy.data.objects[n])
        check(np.abs(a["co"] - b["co"]).max() < 1e-5, n + ": positions")
        check(np.allclose(a["matrix"], b["matrix"]), n + ": transform kept")
        check(a["polys"] == b["polys"], n + ": polygons (n-gons included) kept")
        check(a["materials"] == b["materials"] and a["modifiers"] == b["modifiers"], n + ": materials and modifiers kept")
        for key in ("uv", "color", "sets"):
            if key in a:
                check(key in b and np.allclose(a[key], b[key], atol=1e-6), n + ": " + key)
        if "mask" in a:
            check("mask" in b and np.allclose(a["mask"], b["mask"], atol=1e-4), n + ": mask")

    # Subdivide the box in SculptForge and add a SubTool: the box is rebuilt (transform, material and modifier
    # kept), the new SubTool becomes a new object.
    sculptforge(["blender_receive", "subtool_select name=Box", "subdivide", "subtool_append type=cube name=Extra",
                 "blender_send"])
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "Receive the changed scene: " + bridge.state.message)
    box = bpy.data.objects["Box"]
    check(len(box.data.polygons) > len(before["Box"]["polys"]), "Box: new topology")
    check(np.allclose(np.array(box.matrix_world), before["Box"]["matrix"]), "Box: transform kept")
    check([m.name for m in box.data.materials] == ["Clay"] and box.modifiers.get("Bevel") is not None,
          "Box: material and modifier kept")
    check("Extra" in bpy.data.objects, "the new SubTool is a new object")

    # Sending and receiving work from Sculpt, Edit and Vertex Paint Mode (Edit Mode keeps UVs in its edit mesh):
    # the add-on switches to Object Mode for the transfer and back.
    for mode in ("SCULPT", "EDIT", "VERTEX_PAINT"):
        bpy.ops.object.select_all(action="DESELECT")
        monkey = bpy.data.objects["Suzanne"]
        monkey.select_set(True)
        bpy.context.view_layer.objects.active = monkey
        start = arr(monkey.data.vertices, "co", 3)
        bpy.ops.object.mode_set(mode=mode)
        check(bpy.ops.sculptforge.send() == {"FINISHED"}, mode + ": send")
        sculptforge(["blender_receive", 'stroke points="0,0,-3 0.3,0.3,-3" radius=0.5 strength=1', "blender_send"])
        check(bpy.ops.sculptforge.receive() == {"FINISHED"}, mode + ": receive: " + bridge.state.message)
        check(monkey.mode == mode, mode + ": back in that mode (now %s)" % monkey.mode)
        bpy.ops.object.mode_set(mode="OBJECT")
        moved = np.abs(arr(monkey.data.vertices, "co", 3) - start).max()
        check(moved > 1e-3, mode + ": the sculpt arrived (moved %.3f)" % moved)

    # Apply modifiers: the result is sent, and the modifier is turned off when it comes back.
    bridge.prefs().modifiers = "APPLY"
    bpy.ops.object.select_all(action="DESELECT")
    box.select_set(True)
    bpy.context.view_layer.objects.active = box
    check(bpy.ops.sculptforge.send() == {"FINISHED"}, "send with modifiers applied")
    sculptforge(["blender_receive", "blender_send"])
    bpy.ops.sculptforge.receive()
    check(not box.modifiers["Bevel"].show_viewport, "the applied modifier is turned off")

    addon_utils.disable("sculptforge_bridge")
    print("%d failure(s)" % len(failures))
    sys.exit(1 if failures else 0)


main()
