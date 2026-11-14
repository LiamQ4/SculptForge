// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include <cmath>
#include <cstring>

namespace {
float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::length(a[i] - b[i]));
    return m;
}

sf::Mesh bumpySphere() {
    auto mesh = sf::makeIcosphere(3);
    for (size_t v = 0; v < mesh.vertexCount(); ++v)
        mesh.positions[v] *= 1.0f + .035f * std::sin(float(v) * 2.7f);
    mesh.recomputeAllNormals();
    return mesh;
}
} // namespace

TEST(crkw_mesh_sharpen_mask_visibility_topology_undo) {
    auto mesh = bumpySphere();
    mesh.ensureMask()[0] = 1;
    mesh.mask[1] = .5f;
    auto& hidden = mesh.ensureHidden();
    for (uint32_t t : mesh.vertexTriangles(2)) hidden[t] = 1;
    const auto before = mesh.positions;
    const auto triangles = mesh.triangles;
    auto doc = makeDoc(std::move(mesh));
    sf::CommandRegistry commands;
    const auto r = commands.execute(doc, "crkw_mesh_sharpen");
    CHECK(r.ok);
    CHECK(r.message == "CRKW Mesh Sharpen applied");
    CHECK(maxDistance(doc.mesh.positions, before) > 1e-3f);
    CHECK(doc.mesh.positions[0] == before[0]);
    CHECK(doc.mesh.positions[2] == before[2]);
    CHECK(doc.mesh.positions[1] != before[1]); // half masked: moves, but less than it would
    CHECK(doc.mesh.triangles == triangles);
    for (const auto& p : doc.mesh.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    CHECK(doc.undo.undoCount() == 1);
    const auto after = doc.mesh.positions;
    CHECK(commands.execute(doc, "undo").ok);
    CHECK(doc.mesh.positions == before);
    CHECK(commands.execute(doc, "redo").ok);
    CHECK(doc.mesh.positions == after);
}

TEST(crkw_mesh_sharpen_is_deterministic_and_blend_scales_it) {
    sf::CommandRegistry commands;
    auto a = makeDoc(bumpySphere()), b = makeDoc(bumpySphere()), half = makeDoc(bumpySphere());
    const auto before = a.mesh.positions;
    CHECK(commands.execute(a, "crkw_mesh_sharpen").ok);
    CHECK(commands.execute(b, "crkw_mesh_sharpen").ok);
    CHECK(std::memcmp(a.mesh.positions.data(), b.mesh.positions.data(), before.size() * sizeof(glm::vec3)) == 0);
    CHECK(commands.execute(half, "crkw_mesh_sharpen", {{"blend", 0.5}}).ok);
    for (size_t v = 0; v < before.size(); ++v)
        CHECK(glm::length((half.mesh.positions[v] - before[v]) * 2.0f - (a.mesh.positions[v] - before[v])) < 1e-5f);
    // Curvedness weights differently.
    auto c = makeDoc(bumpySphere());
    CHECK(commands.execute(c, "crkw_mesh_sharpen", {{"type", std::string("curvedness")}}).ok);
    CHECK(maxDistance(c.mesh.positions, a.mesh.positions) > 1e-4f);
}

TEST(crkw_mesh_sharpen_pins_open_borders) {
    auto mesh = sf::makePlane(16);
    for (auto& p : mesh.positions) p.z = .1f * std::sin(p.x * 5.0f) * std::cos(p.y * 4.0f);
    mesh.recomputeAllNormals();
    std::vector<uint32_t> border;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v)
        if (std::abs(std::abs(mesh.positions[v].x) - 1.0f) < 1e-4f || std::abs(std::abs(mesh.positions[v].y) - 1.0f) < 1e-4f)
            border.push_back(v);
    CHECK(!border.empty());
    sf::CommandRegistry commands;
    auto pinned = makeDoc(mesh), loose = makeDoc(mesh);
    CHECK(commands.execute(pinned, "crkw_mesh_sharpen").ok);
    CHECK(commands.execute(loose, "crkw_mesh_sharpen", {{"pin_borders", false}}).ok);
    float pinnedMove = 0, looseMove = 0;
    for (uint32_t v : border) {
        pinnedMove = std::max(pinnedMove, glm::length(pinned.mesh.positions[v] - mesh.positions[v]));
        looseMove = std::max(looseMove, glm::length(loose.mesh.positions[v] - mesh.positions[v]));
    }
    CHECK(pinnedMove == 0.0f);
    CHECK(looseMove > 1e-4f);
    CHECK(maxDistance(pinned.mesh.positions, mesh.positions) > 1e-4f); // the inside still moved
}

TEST(crkw_mesh_sharpen_refusals_and_no_ops) {
    sf::CommandRegistry commands;
    auto doc = makeDoc(sf::makeIcosphere(2));
    CHECK(commands.execute(doc, "crkw_mesh_sharpen", {{"iterations", 0.0}}).ok);
    CHECK(commands.execute(doc, "crkw_mesh_sharpen", {{"blend", 0.0}}).ok);
    CHECK(doc.undo.undoCount() == 0);
    CHECK(!commands.execute(doc, "crkw_mesh_sharpen", {{"type", std::string("gaussian")}}).ok);
    doc.sculptor.dyntopo.enabled = true;
    CHECK(!commands.execute(doc, "crkw_mesh_sharpen").ok);
    doc.sculptor.dyntopo.enabled = false;
    doc.activeLayer = doc.mesh.addLayer("CRKW");
    doc.mesh.layers[0].weight = 0;
    CHECK(!commands.execute(doc, "crkw_mesh_sharpen").ok);
    CHECK(doc.undo.undoCount() == 0);
}

TEST(crkw_mesh_sharpen_writes_to_the_active_sculpt_layer) {
    sf::CommandRegistry commands;
    auto doc = makeDoc(bumpySphere());
    doc.activeLayer = doc.mesh.addLayer("CRKW");
    doc.mesh.layers[0].weight = .6f;
    const auto before = doc.mesh.positions;
    CHECK(commands.execute(doc, "crkw_mesh_sharpen").ok);
    const auto after = doc.mesh.positions;
    doc.mesh.evaluateAll();
    CHECK(maxDistance(after, doc.mesh.positions) < 1e-6f);
    CHECK(commands.execute(doc, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    CHECK(maxDistance(doc.mesh.positions, before) < 1e-5f); // all of it went into the layer
}

// Like Classic: it works on the level being sculpted and the levels above keep their detail on top of it.
TEST(crkw_mesh_sharpen_works_on_the_current_subdivision_level) {
    sf::CommandRegistry commands;
    auto doc = makeDoc(sf::makeCube(4));
    CHECK(commands.execute(doc, "subdiv_divide").ok);
    CHECK(commands.execute(doc, "subdiv_divide").ok);
    CHECK(commands.execute(doc, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(sf::subdivLevelText(doc.mesh) == "Level 2/3");
    const auto level2 = doc.mesh.positions;
    CHECK(commands.execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    const auto top = doc.mesh.positions;
    CHECK(commands.execute(doc, "subdiv_level", {{"level", 2.0}}).ok);

    CHECK(commands.execute(doc, "crkw_mesh_sharpen").ok);
    CHECK(sf::subdivLevelText(doc.mesh) == "Level 2/3");
    const auto sharpened = doc.mesh.positions;
    CHECK(maxDistance(sharpened, level2) > 1e-3f);
    CHECK(commands.execute(doc, "undo").ok);
    CHECK(maxDistance(doc.mesh.positions, level2) < 1e-6f);
    CHECK(commands.execute(doc, "redo").ok);
    CHECK(maxDistance(doc.mesh.positions, sharpened) < 1e-6f);
    CHECK(commands.execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(sf::subdivLevelText(doc.mesh) == "Level 3/3");
    CHECK(maxDistance(doc.mesh.positions, top) > 1e-3f); // the change reached the top level
    CHECK(commands.execute(doc, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(maxDistance(doc.mesh.positions, sharpened) < 1e-4f);
}
