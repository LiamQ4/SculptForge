// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Commands.h"
#include "core/Primitives.h"
#include <cmath>

TEST(ckw_polish_mask_visibility_topology_undo) {
    auto mesh = sf::makeIcosphere(3);
    for (size_t v = 0; v < mesh.vertexCount(); ++v)
        mesh.positions[v] *= 1.0f + .035f * std::sin(float(v) * 2.7f);
    mesh.recomputeAllNormals();
    mesh.ensureMask()[0] = 1;
    auto& hidden = mesh.ensureHidden();
    for (uint32_t t : mesh.vertexTriangles(2)) hidden[t] = 1;
    const auto before = mesh.positions;
    const auto triangles = mesh.triangles;
    const auto mask = mesh.mask;
    const auto visibility = mesh.hidden;
    auto doc = makeDoc(std::move(mesh));
    sf::CommandRegistry commands;
    CHECK(commands.execute(doc, "ckw_clay_polish").ok);
    CHECK(doc.mesh.positions != before);
    CHECK(doc.mesh.positions[0] == before[0]);
    CHECK(doc.mesh.positions[2] == before[2]);
    CHECK(doc.mesh.triangles == triangles);
    CHECK(doc.mesh.mask == mask);
    CHECK(doc.mesh.hidden == visibility);
    for (const auto& p : doc.mesh.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    CHECK(doc.undo.undoCount() == 1);
    const auto after = doc.mesh.positions;
    CHECK(commands.execute(doc, "undo").ok);
    CHECK(doc.mesh.positions == before);
    CHECK(commands.execute(doc, "redo").ok);
    CHECK(doc.mesh.positions == after);
}

TEST(ckw_polish_layers_disabled_passes_and_dyntopo) {
    auto doc = makeDoc(sf::makeIcosphere(2));
    sf::CommandRegistry commands;
    CHECK(commands.execute(doc, "ckw_clay_polish", {{"soft_passes", 0.0}, {"sharp_passes", 0.0}}).ok);
    CHECK(doc.undo.undoCount() == 0);
    doc.sculptor.dyntopo.enabled = true;
    CHECK(!commands.execute(doc, "ckw_clay_polish").ok);
    doc.sculptor.dyntopo.enabled = false;
    doc.activeLayer = doc.mesh.addLayer("CKW");
    doc.mesh.layers[0].weight = .6f;
    const auto before = doc.mesh.positions;
    CHECK(commands.execute(doc, "ckw_clay_polish").ok);
    const auto after = doc.mesh.positions;
    doc.mesh.evaluateAll();
    for (size_t i = 0; i < after.size(); ++i) CHECK(glm::length(after[i] - doc.mesh.positions[i]) < 1e-6f);
    CHECK(commands.execute(doc, "undo").ok);
    for (size_t i = 0; i < before.size(); ++i) CHECK(glm::length(before[i] - doc.mesh.positions[i]) < 1e-6f);
    doc.mesh.layers[0].weight = 0;
    CHECK(!commands.execute(doc, "ckw_clay_polish").ok);
}

TEST(ckw_polish_shape_correction_reduces_smoothing_shrinkage) {
    auto smooth = makeDoc(sf::makeIcosphere(2));
    auto preserve = makeDoc(sf::makeIcosphere(2));
    const auto original = smooth.mesh.positions;
    sf::CommandRegistry commands;
    CHECK(commands.execute(smooth, "ckw_clay_polish", {{"sharpness", 0.0}, {"preserve_shape", 0.0}}).ok);
    CHECK(commands.execute(preserve, "ckw_clay_polish", {{"sharpness", 0.0}, {"preserve_shape", 1.0}}).ok);
    double smoothDistance = 0, preservedDistance = 0;
    for (size_t i = 0; i < original.size(); ++i) {
        smoothDistance += glm::length(smooth.mesh.positions[i] - original[i]);
        preservedDistance += glm::length(preserve.mesh.positions[i] - original[i]);
    }
    CHECK(smoothDistance > 0);
    CHECK(preservedDistance < smoothDistance);
}
