// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"
#include "core/Texture.h"
#include <cmath>
#include <algorithm>
#include <set>

using namespace sf;

TEST(crkw_auto_retopo_preserves_history_and_projects_only_frozen_source) {
    CommandRegistry commands;
    Document doc = makeDoc(makeIcosphere(4));
    CHECK(commands.execute(doc, "stroke", {{"points", std::string("-.4,.2,2 .4,.2,2")},
        {"brush", std::string("Standard")}, {"radius", .2}, {"strength", .5}}).ok);
    CHECK(commands.execute(doc, "mask_all").ok);
    const auto positions = doc.mesh.positions;
    const auto triangles = doc.mesh.triangles;
    const size_t history = doc.undo.undoCount();
    const uint32_t sourceId = doc.activeSubtoolInfo().id;
    doc.addSubtool(makeIcosphere(2, 10), "Unrelated source", false);
    const auto result = commands.execute(doc, "crkw_retopo_project", {{"target", 100.0}, {"distance", .1}});
    std::printf("%s\n", result.message.c_str());
    CHECK(result.ok);
    if (!result.ok) return;
    CHECK(doc.subtoolCount() == 3);
    CHECK(doc.activeSubtool == 2);
    CHECK(doc.subtools[0].id == sourceId);
    CHECK(!doc.subtools[0].visible);
    CHECK(doc.subtoolMesh(0).positions == positions);
    CHECK(doc.subtoolMesh(0).triangles == triangles);
    CHECK(doc.subtools[0].undo.undoCount() == history);
    CHECK(doc.mesh.mask.empty());
    CHECK(subdivLevelCount(doc.mesh) >= 2);
    CHECK(doc.mesh.vertexCount() > positions.size() / 2);
    CHECK(doc.mesh.vertexCount() < positions.size() * 2);
    TriangleBVH reference; reference.build(doc.subtoolMesh(0));
    for (size_t i = 0; i < doc.mesh.vertexCount(); i += 97) {
        SurfaceHit hit;
        CHECK(reference.closestPoint(doc.mesh.positions[i], .02f, hit));
        CHECK(hit.distance < .002f);
    }
    const auto projected = doc.mesh.positions;
    const int highest = subdivCurrentLevel(doc.mesh);
    std::string error;
    CHECK(subdivSetLevel(doc.mesh, 0, error));
    CHECK(doc.mesh.vertexCount() < positions.size() / 2);
    CHECK(subdivSetLevel(doc.mesh, highest, error));
    CHECK(doc.mesh.positions == projected);
    doc.selectSubtool(0);
    CHECK(doc.undo.undoCount() == history);
    CHECK(commands.execute(doc, "undo").ok);
    CHECK(std::all_of(doc.mesh.mask.begin(), doc.mesh.mask.end(), [](float value) { return value == 0; }));
}

TEST(crkw_auto_retopo_invalid_parameters_leave_source_unchanged) {
    CommandRegistry commands;
    Document doc = makeDoc(makeIcosphere(3));
    const auto original = doc.mesh.positions;
    CHECK(!commands.execute(doc, "crkw_retopo_project", {{"target", -1.0}}).ok);
    CHECK(!commands.execute(doc, "crkw_retopo_project", {{"max_points", 128.0}}).ok);
    CHECK(doc.mesh.positions == original);
    CHECK(doc.subtoolCount() == 1);
    CHECK(!doc.undo.canUndo());
}

TEST(crkw_auto_retopo_transfers_texture_onto_subdivision_uvs) {
    CommandRegistry commands;
    Document doc = makeDoc(makeIcosphere(3));
    CHECK(commands.execute(doc, "uv_auto", {{"resolution", 1024.0}}).ok);
    doc.mesh.texture = makeTexture(64, 64);
    for (size_t i = 0; i < doc.mesh.texture->pixels.size(); i += 4) {
        doc.mesh.texture->pixels[i] = 180; doc.mesh.texture->pixels[i + 1] = 70; doc.mesh.texture->pixels[i + 2] = 30;
    }
    doc.mesh.texture->markAllDirty();
    doc.mesh.paintToTexture = true;
    const auto sourceTexture = doc.mesh.texture;
    const auto result = commands.execute(doc, "crkw_retopo_project", {{"target", 40.0}, {"distance", .1}});
    CHECK(result.ok);
    if (!result.ok) { std::printf("%s\n", result.message.c_str()); return; }
    CHECK(hasTexture(doc.mesh));
    CHECK(doc.mesh.hasUvs());
    CHECK(doc.mesh.texture != sourceTexture);
    CHECK(doc.subtoolMesh(0).texture == sourceTexture);
    CHECK(doc.mesh.texture->width == 64);
    bool transferred = false;
    for (size_t i = 0; i < doc.mesh.texture->pixels.size(); i += 4)
        transferred |= doc.mesh.texture->pixels[i] == 180 && doc.mesh.texture->pixels[i + 1] == 70 && doc.mesh.texture->pixels[i + 2] == 30;
    CHECK(transferred);
}

TEST(crkw_auto_retopo_keeps_polygroups_by_default_at_every_level) {
    CommandRegistry commands;
    Document doc = makeDoc(makeCube(12));
    CHECK(commands.execute(doc, "polygroup_auto_by_normals", {{"angle", 40.0}}).ok);
    const auto sourceGroups = doc.mesh.polygroups;
    const std::set<uint32_t> expected(sourceGroups.begin(), sourceGroups.end());
    CHECK(expected.size() == 6);
    const auto result = commands.execute(doc, "crkw_retopo_project", {{"target", 150.0}, {"distance", .1}});
    std::printf("%s\n", result.message.c_str());
    CHECK(result.ok);
    if (!result.ok) return;
    CHECK(doc.subtoolMesh(0).polygroups == sourceGroups);
    CHECK(subdivLevelCount(doc.mesh) >= 2);
    const int highest = subdivCurrentLevel(doc.mesh);
    for (int level = 0; level <= highest; ++level) {
        std::string error;
        CHECK(subdivSetLevel(doc.mesh, level, error));
        CHECK(doc.mesh.hasPolygroups());
        const std::set<uint32_t> actual(doc.mesh.polygroups.begin(), doc.mesh.polygroups.end());
        CHECK(actual == expected);
        for (size_t t = 0; t < doc.mesh.triangleCount(); ++t) {
            const uint32_t partner = doc.mesh.quadPartners.empty() ? Mesh::kNoQuad : doc.mesh.quadPartners[t];
            if (partner != Mesh::kNoQuad) CHECK(doc.mesh.polygroups[t] == doc.mesh.polygroups[partner]);
        }
    }
}
