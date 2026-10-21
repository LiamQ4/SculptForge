# SculptForge - Python API smoke test (run by ctest when SCULPTFORGE_BUILD_PYTHON=ON).
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import tempfile

import sculptforge as sf

doc = sf.Document()
assert "triangles" in sf.run(doc, "new_sphere", subdivisions=3)
verts, tris = doc.vertices, doc.triangles
assert verts.shape == (642, 3) and str(verts.dtype) == "float32", verts.shape
assert tris.shape == (1280, 3) and str(tris.dtype) == "uint32", tris.shape

# Optional attributes: None until set, then NumPy copies; they survive subdivision.
assert doc.mask is None and doc.colors is None and doc.polygroups is None and doc.hidden is None
doc.mask = [1.0 if v[1] > 0 else 0.0 for v in verts]
doc.colors = [[1.0, 0.0, 0.0]] * len(verts)
doc.polygroups = [i % 3 for i in range(len(tris))]
assert doc.mask.shape == (642,) and str(doc.mask.dtype) == "float32" and doc.colors.shape == (642, 3)
assert doc.polygroups.shape == (1280,) and str(doc.polygroups.dtype) == "uint32"
sf.run(doc, "subdivide")
assert doc.mask.shape == (doc.vertices.shape[0],) and doc.polygroups.shape == (doc.triangles.shape[0],)
try:
    doc.mask = [0.0, 1.0]
    raise AssertionError("expected ValueError")
except ValueError:
    pass
doc.colors = None
assert doc.colors is None
sf.run(doc, "new_sphere", subdivisions=3)
verts = doc.vertices

sf.run(doc, "layer_add", name="Smile")
assert doc.layers == [("Smile", 1.0)], doc.layers
assert doc.layer_delta(0).shape == (642, 3)

names = {c["name"] for c in sf.commands()}
assert {"remesh", "export_usd", "unreal_send", "image_to_3d", "text_to_3d"} <= names, names

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, "head.usda")
    messages = sf.run_script(doc, f'remesh resolution=48; export_usd path="{out}"')
    assert len(messages) == 2 and os.path.getsize(out) > 0

try:
    sf.run(doc, "open", path="/no/such/file.obj")
    raise AssertionError("expected CommandError")
except sf.CommandError:
    pass

# Bring your own mesh in from a pipeline.
doc.set_mesh([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]])
assert doc.triangles.shape == (4, 3)
assert "Document" in repr(doc) and sf.__version__
assert '"name":"remesh"' in sf.tool_schema()
print("python api ok:", sf.__version__)
