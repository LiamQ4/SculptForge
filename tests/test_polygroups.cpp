// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygroups and visibility: grouping commands, hide/isolate/show/invert,
// rectangle hiding, delete hidden, undo, and that picking and brushes skip
// hidden geometry.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Polygroups.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace sf;

namespace {

// Unit cube with shared corners: 8 vertices, 12 triangles (outward winding).
Mesh makeCube() {
    Mesh m;
    for (int i = 0; i < 8; ++i) m.positions.push_back(glm::vec3(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1));
    const glm::uvec3 tris[12] = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4},
                                 {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    m.triangles.assign(std::begin(tris), std::end(tris));
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

// Two separate spheres: the first (x < 0) has triangles [0, half), the second the rest.
Mesh makeTwoSpheres(int level = 2) {
    Mesh a = makeIcosphere(level), m;
    for (int s = 0; s < 2; ++s) {
        const uint32_t base = uint32_t(m.positions.size());
        for (const auto& p : a.positions) m.positions.push_back(p + glm::vec3(s ? 1.5f : -1.5f, 0, 0));
        for (const auto& t : a.triangles) m.triangles.push_back(t + glm::uvec3(base));
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

size_t hiddenCount(const Mesh& m) {
    size_t n = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) n += m.triangleHidden(t) ? 1 : 0;
    return n;
}

// Away from a rectangle edge at 0 (rounding there may go either way).
bool onSide(float coordinate) { return std::abs(coordinate) > 1e-4f; }

glm::vec3 centroid(const Mesh& m, uint32_t t) {
    const auto& tri = m.triangles[t];
    return (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
}

} // namespace

TEST(polygroup_commands_registered) {
    CommandRegistry reg;
    for (const char* name : {"polygroup_auto_by_normals", "polygroup_by_connectivity", "polygroup_from_mask",
                             "polygroup_clear", "polygroup_merge_visible", "hide_polygroup", "isolate_polygroup",
                             "show_all", "invert_visibility", "hide_rect", "delete_hidden"})
        CHECK(reg.find(name) != nullptr);
    CHECK(reg.toolSchemaJson().find("\"isolate_polygroup\"") != std::string::npos);
}

TEST(polygroups_by_normals) {
    Document d = makeDoc(makeCube());
    CommandRegistry reg;
    CHECK(reg.execute(d, "polygroup_auto_by_normals", {{"angle", 30.0}}).ok);
    CHECK(d.mesh.polygroups.size() == 12 && visiblePolygroupCount(d.mesh) == 6);
    // Each side's two triangles share a group, different sides differ.
    for (uint32_t t = 0; t < 12; t += 2) CHECK(d.mesh.polygroups[t] == d.mesh.polygroups[t + 1]);
    std::set<uint32_t> ids(d.mesh.polygroups.begin(), d.mesh.polygroups.end());
    CHECK(ids.size() == 6 && *ids.begin() == 0 && *ids.rbegin() == 5);
    CHECK(d.mesh.facesDirty);
    // Undo restores the single default group.
    CHECK(reg.execute(d, "undo").ok);
    for (uint32_t t = 0; t < 12; ++t) CHECK(d.mesh.polygroupAt(t) == 0);
    CHECK(reg.execute(d, "redo").ok);
    CHECK(visiblePolygroupCount(d.mesh) == 6);

    // A smooth sphere with a wide angle stays one group (and frees the attribute).
    Document s = makeDoc(makeIcosphere(3));
    CHECK(reg.execute(s, "polygroup_auto_by_normals", {{"angle", 180.0}}).ok);
    CHECK(!s.mesh.hasPolygroups() && visiblePolygroupCount(s.mesh) == 1);
    // A narrow angle splits it into patches.
    CHECK(reg.execute(s, "polygroup_auto_by_normals", {{"angle", 20.0}}).ok);
    CHECK(visiblePolygroupCount(s.mesh) > 10);
}

TEST(polygroups_by_connectivity_and_merge) {
    Document d = makeDoc(makeTwoSpheres());
    const uint32_t half = uint32_t(d.mesh.triangleCount() / 2);
    CommandRegistry reg;
    CHECK(reg.execute(d, "polygroup_by_connectivity").ok);
    CHECK(visiblePolygroupCount(d.mesh) == 2);
    CHECK(d.mesh.polygroupAt(0) == 0 && d.mesh.polygroupAt(half) == 1);
    bool consistent = true;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t)
        consistent = consistent && d.mesh.polygroupAt(t) == (t < half ? 0u : 1u);
    CHECK(consistent);
    CHECK(polygroupIsolated(d.mesh, 0) == false);

    CHECK(reg.execute(d, "polygroup_merge_visible").ok);
    CHECK(visiblePolygroupCount(d.mesh) == 1 && !d.mesh.hasPolygroups());
    CHECK(reg.execute(d, "undo").ok);
    CHECK(visiblePolygroupCount(d.mesh) == 2);

    // Grouping only touches visible triangles; hidden ones keep their ids.
    CHECK(reg.execute(d, "isolate_polygroup", {{"group", 1.0}}).ok);
    CHECK(reg.execute(d, "polygroup_auto_by_normals", {{"angle", 15.0}}).ok);
    CHECK(d.mesh.polygroupAt(0) == 0);
    for (uint32_t t = half; t < d.mesh.triangleCount(); ++t) CHECK(d.mesh.polygroupAt(t) >= 1);
    CHECK(visiblePolygroupCount(d.mesh) > 1);

    CHECK(reg.execute(d, "polygroup_clear").ok);
    CHECK(!d.mesh.hasPolygroups());
}

TEST(polygroup_from_mask) {
    Document d = makeDoc(makeIcosphere(3));
    CommandRegistry reg;
    CHECK(!reg.execute(d, "polygroup_from_mask").ok); // nothing masked
    d.mesh.ensureMask();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].y > 0.0f ? 1.0f : 0.0f;
    CHECK(reg.execute(d, "polygroup_from_mask").ok);
    CHECK(visiblePolygroupCount(d.mesh) == 2);
    int wrong = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) {
        const glm::vec3 c = centroid(d.mesh, t);
        if (std::abs(c.y) > 0.2f && d.mesh.polygroupAt(t) != (c.y > 0.0f ? 1u : 0u)) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(d.mesh.hasMask()); // the mask itself is left alone
}

TEST(visibility_isolate_hide_invert_show) {
    Document d = makeDoc(makeTwoSpheres());
    const size_t n = d.mesh.triangleCount(), half = n / 2;
    CommandRegistry reg;
    CHECK(reg.execute(d, "polygroup_by_connectivity").ok);

    CHECK(reg.execute(d, "isolate_polygroup", {{"group", 1.0}}).ok);
    CHECK(hiddenCount(d.mesh) == half && d.mesh.triangleHidden(0) && !d.mesh.triangleHidden(uint32_t(half)));
    CHECK(polygroupIsolated(d.mesh, 1) && !polygroupIsolated(d.mesh, 0));
    CHECK(d.mesh.vertexHidden(0) && !d.mesh.vertexHidden(d.mesh.triangles[half].x));
    CHECK(!reg.execute(d, "isolate_polygroup", {{"group", 7.0}}).ok);

    CHECK(reg.execute(d, "invert_visibility").ok);
    CHECK(hiddenCount(d.mesh) == half && !d.mesh.triangleHidden(0) && d.mesh.triangleHidden(uint32_t(half)));

    // Hiding the last visible group is refused (nothing would be left on screen).
    CHECK(!reg.execute(d, "hide_polygroup", {{"group", 0.0}}).ok);
    CHECK(reg.execute(d, "show_all").ok);
    CHECK(!d.mesh.hasHidden());
    CHECK(reg.execute(d, "hide_polygroup", {{"group", 0.0}}).ok);
    CHECK(hiddenCount(d.mesh) == half && d.mesh.triangleHidden(0));
    CHECK(!reg.execute(d, "hide_polygroup", {{"group", 5.0}}).ok);

    // With nothing hidden, inverting would hide everything.
    CHECK(reg.execute(d, "show_all").ok);
    CHECK(!reg.execute(d, "invert_visibility").ok);

    // Each step is undoable: show_all -> hide 0 -> show_all -> invert -> isolate.
    CHECK(reg.execute(d, "undo").ok); // show_all
    CHECK(hiddenCount(d.mesh) == half && d.mesh.triangleHidden(0));
    CHECK(reg.execute(d, "undo").ok); // hide_polygroup
    CHECK(hiddenCount(d.mesh) == 0);
    CHECK(reg.execute(d, "undo").ok); // show_all
    CHECK(reg.execute(d, "undo").ok); // invert
    CHECK(polygroupIsolated(d.mesh, 1));
}

TEST(visibility_hide_rect) {
    Document d = makeDoc(makeIcosphere(3));
    CommandRegistry reg;
    // Default view: the mesh seen from the front, NDC spanning its bounds. Keep x < 0.
    CHECK(reg.execute(d, "hide_rect", {{"x0", -1.0}, {"y0", -1.0}, {"x1", 0.0}, {"y1", 1.0}}).ok);
    int wrong = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t)
        if (onSide(centroid(d.mesh, t).x) && d.mesh.triangleHidden(t) != (centroid(d.mesh, t).x > 0.0f)) ++wrong;
    CHECK(wrong == 0 && hiddenCount(d.mesh) > 0);
    CHECK(reg.execute(d, "undo").ok);
    CHECK(hiddenCount(d.mesh) == 0);

    // Alt (inside): hide the left half instead.
    CHECK(reg.execute(d, "hide_rect", {{"x0", -1.0}, {"y0", -1.0}, {"x1", 0.0}, {"y1", 1.0}, {"inside", true}}).ok);
    wrong = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t)
        if (onSide(centroid(d.mesh, t).x) && d.mesh.triangleHidden(t) != (centroid(d.mesh, t).x <= 0.0f)) ++wrong;
    CHECK(wrong == 0);
    CHECK(reg.execute(d, "show_all").ok);

    // An explicit view-projection (identity: NDC = world) keeping y < 0; rectangles are cumulative.
    const std::string identity = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";
    CHECK(reg.execute(d, "hide_rect", {{"x0", -2.0}, {"y0", -2.0}, {"x1", 2.0}, {"y1", 0.0}, {"view_proj", identity}}).ok);
    CHECK(reg.execute(d, "hide_rect", {{"x0", 0.0}, {"y0", -2.0}, {"x1", 2.0}, {"y1", 2.0}, {"view_proj", identity}}).ok);
    wrong = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) {
        const glm::vec3 c = centroid(d.mesh, t);
        if (onSide(c.x) && onSide(c.y) && d.mesh.triangleHidden(t) != !(c.y <= 0.0f && c.x >= 0.0f)) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(!reg.execute(d, "hide_rect", {{"view_proj", std::string("1 2 3")}}).ok);
    // A rectangle on empty space would hide everything: refused.
    CHECK(!reg.execute(d, "hide_rect", {{"x0", 5.0}, {"y0", 5.0}, {"x1", 6.0}, {"y1", 6.0}, {"view_proj", identity}}).ok);
}

TEST(delete_hidden_compacts_attributes) {
    Document d = makeDoc(makeTwoSpheres());
    const size_t nv = d.mesh.vertexCount(), nt = d.mesh.triangleCount();
    CommandRegistry reg;
    CHECK(!reg.execute(d, "delete_hidden").ok); // nothing hidden
    CHECK(reg.execute(d, "polygroup_by_connectivity").ok);
    Mesh& m = d.mesh;
    m.ensureMask();
    m.ensureColors();
    for (uint32_t v = 0; v < nv; ++v) {
        m.mask[v] = m.positions[v].y > 0.0f ? 1.0f : 0.0f;
        m.colors[v] = glm::vec3(m.positions[v].x > 0.0f ? 1.0f : 0.0f, 0.5f, 0.25f);
    }
    m.uvs.resize(nt * 3);
    for (size_t i = 0; i < m.uvs.size(); ++i) m.uvs[i] = glm::vec2(float(i), 0.0f);
    m.addLayer("Detail");
    for (uint32_t v = 0; v < nv; ++v) m.layers[0].delta[v] = glm::vec3(0, 0, m.positions[v].x);
    m.evaluateAll();
    m.recomputeAllNormals();

    CHECK(reg.execute(d, "hide_polygroup", {{"group", 0.0}}).ok);
    CHECK(reg.execute(d, "delete_hidden").ok);
    CHECK(d.mesh.vertexCount() == nv / 2 && d.mesh.triangleCount() == nt / 2);
    CHECK(!d.mesh.hasHidden());
    CHECK(d.mesh.mask.size() == nv / 2 && d.mesh.colors.size() == nv / 2);
    CHECK(d.mesh.polygroups.size() == nt / 2 && d.mesh.uvs.size() == nt * 3 / 2);
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].delta.size() == nv / 2 && d.mesh.base.size() == nv / 2);
    int wrong = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3& p = d.mesh.basePositions()[v];
        if (p.x <= 0.0f) ++wrong; // only the right sphere is left
        if (d.mesh.mask[v] != (p.y > 0.0f ? 1.0f : 0.0f) || d.mesh.colors[v].x != 1.0f) ++wrong;
        if (d.mesh.layers[0].delta[v].z != p.x) ++wrong;
    }
    CHECK(wrong == 0);
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) CHECK(d.mesh.polygroups[t] == 1u);
    CHECK(d.mesh.uvs[0].x == float(nt / 2 * 3)); // corner UVs follow their triangle
    // Picking still works on the compacted mesh (BVH rebuilt).
    RayHit hit;
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(1.5f, 0, 5), glm::vec3(0, 0, -1), hit));

    CHECK(reg.execute(d, "undo").ok);
    CHECK(d.mesh.vertexCount() == nv && d.mesh.triangleCount() == nt && hiddenCount(d.mesh) == nt / 2);
    CHECK(d.mesh.layers.size() == 1 && d.mesh.mask.size() == nv);
}

TEST(hidden_geometry_skipped_by_pick_and_brushes) {
    Document d = makeDoc(makeTwoSpheres(3));
    CommandRegistry reg;
    CHECK(reg.execute(d, "polygroup_by_connectivity").ok);
    CHECK(reg.execute(d, "isolate_polygroup", {{"group", 1.0}}).ok);

    // A ray at the hidden left sphere passes through; the visible one is still hit.
    RayHit hit;
    CHECK(!d.bvh.raycast(d.mesh, glm::vec3(-1.5f, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(1.5f, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(d.mesh.polygroupAt(hit.triangle) == 1);

    // brushVertices excludes hidden vertices.
    std::vector<uint32_t> verts;
    brushVertices(d.mesh, d.bvh, glm::vec3(-1.5f, 0, 1), 0.6f, verts);
    CHECK(verts.empty());

    // A big dab over both spheres moves only the visible one (symmetry X on).
    const std::vector<glm::vec3> before = d.mesh.positions;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    DabParams p;
    p.center = glm::vec3(1.5f, 0, 1);
    p.radius = 1.0f;
    p.strength = 1.0f;
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{});
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    int movedHidden = 0, movedVisible = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (d.mesh.positions[v] == before[v]) continue;
        if (d.mesh.vertexHidden(v)) ++movedHidden;
        else ++movedVisible;
    }
    CHECK(movedHidden == 0 && movedVisible > 0);
    // Shown again, the hidden sphere was never touched.
    CHECK(reg.execute(d, "show_all").ok);
    for (uint32_t v = 0; v < d.mesh.vertexCount() / 2; ++v) CHECK(d.mesh.positions[v] == before[v]);
}
