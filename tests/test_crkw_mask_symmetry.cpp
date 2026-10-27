// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Commands.h"
#include "core/Primitives.h"

namespace {
// Masked vertices on the -x and +x sides.
std::pair<int, int> maskedSides(const sf::Mesh& m) {
    int left = 0, right = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        if (m.maskAt(v) < 0.5f) continue;
        if (m.positions[v].x < -0.05f) ++left;
        if (m.positions[v].x > 0.05f) ++right;
    }
    return {left, right};
}
} // namespace

// CRKW: MaskRect / MaskLasso follow symmetry, like a MaskPen stroke.
TEST(crkw_mask_shapes_follow_symmetry) {
    sf::CommandRegistry commands;
    const sf::Args rect{{"x0", -1.0}, {"y0", -1.0}, {"x1", -0.4}, {"y1", 1.0}};
    auto plain = makeDoc(sf::makeIcosphere(4));
    CHECK(commands.execute(plain, "mask_rect", rect).ok);
    const auto [l0, r0] = maskedSides(plain.mesh);
    CHECK(l0 > 0 && r0 == 0);

    auto mirrored = makeDoc(sf::makeIcosphere(4));
    sf::Args sym = rect;
    sym["symmetry"] = std::string("x");
    CHECK(commands.execute(mirrored, "mask_rect", sym).ok);
    const auto [l1, r1] = maskedSides(mirrored.mesh);
    CHECK(l1 == l0);
    CHECK(r1 == l0); // the icosphere is symmetric: the mirror image is the same size

    auto lasso = makeDoc(sf::makeIcosphere(4));
    CHECK(commands.execute(lasso, "mask_lasso", {{"points", std::string("-1 -1 -0.4 -1 -0.4 1 -1 1")}, {"symmetry", std::string("x")}}).ok);
    CHECK(maskedSides(lasso.mesh) == std::make_pair(l0, l0));
    CHECK(commands.execute(lasso, "mask_lasso", {{"points", std::string("-1 -1 -0.4 -1 -0.4 1 -1 1")}, {"unmask", true}, {"symmetry", std::string("x")}}).ok);
    CHECK(maskedSides(lasso.mesh) == std::make_pair(0, 0));
    CHECK(!commands.execute(lasso, "mask_rect", {{"symmetry", std::string("w")}}).ok);
}
