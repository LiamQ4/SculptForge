// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orbit camera: turntable and free (ZBrush) rotation, snapping to axis views.
#include "TestHarness.h"

#include "render/Camera.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

using namespace sf;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

glm::vec3 up(const Camera& c) { return c.orientation() * glm::vec3(0, 1, 0); }

} // namespace

TEST(camera_turntable_stays_upright) {
    Camera c;
    c.orbit(0.0f, 1000.0f); // far past the top
    CHECK(c.pitch >= -glm::radians(89.01f));
    CHECK(up(c).y > 0.0f);
    CHECK(c.roll == 0.0f);
}

TEST(camera_free_rotation_goes_over_the_top) {
    Camera c;
    const float quarter = glm::half_pi<float>() / 0.008f; // pixels for 90 degrees
    c.orbitFree(0.0f, 2.0f * quarter);                     // tilt 180 degrees: upside down, looking from behind
    CHECK(up(c).y < -0.99f);
    CHECK(near(c.forward(), glm::vec3(0, 0, 1), 1e-3f));
    // Sideways drags turn about the screen's vertical axis, whatever the tilt.
    Camera d;
    d.orbitFree(0.0f, quarter * 0.5f);
    const glm::vec3 screenUp = up(d);
    d.orbitFree(quarter, 0.0f);
    CHECK(near(up(d), screenUp, 1e-3f));
}

TEST(camera_orientation_round_trips_through_angles) {
    Camera c;
    const glm::quat q = glm::normalize(glm::quat(0.3f, -0.5f, 0.7f, 0.2f));
    c.setOrientation(q);
    const glm::quat back = c.orientation();
    CHECK(std::abs(std::abs(glm::dot(back, q)) - 1.0f) < 1e-4f);
}

TEST(camera_snaps_to_axis_views) {
    Camera c;
    c.setOrientation(glm::angleAxis(glm::radians(37.0f), glm::vec3(0, 1, 0)) *
                     glm::angleAxis(glm::radians(-20.0f), glm::vec3(1, 0, 0)));
    c.setOrientation(Camera::snapped(c.orientation()));
    CHECK(near(c.forward(), glm::vec3(0, 0, -1)));
    CHECK(near(up(c), glm::vec3(0, 1, 0)));
    // Near the top view: looks straight down, with an axis as screen up.
    c.setOrientation(glm::angleAxis(glm::radians(-80.0f), glm::vec3(1, 0, 0)) *
                     glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 0, 1)));
    c.setOrientation(Camera::snapped(c.orientation()));
    CHECK(near(c.forward(), glm::vec3(0, -1, 0)));
    const glm::vec3 u = glm::abs(up(c));
    CHECK(std::max({u.x, u.y, u.z}) > 0.9999f);
}

TEST(camera_turntable_after_free_rotation_stands_upright) {
    Camera c;
    c.orbitFree(300.0f, 120.0f);
    c.orbitFree(-50.0f, 400.0f);
    const glm::vec3 f = c.forward();
    c.orbit(0.0f, 0.0f);
    CHECK(c.roll == 0.0f);
    CHECK(up(c).y > 0.0f);
    // The view direction is kept (clamped short of straight up / down).
    if (std::abs(f.y) < 0.99f) CHECK(near(c.forward(), f, 1e-3f));
}

TEST(camera_orthographic_rays_and_brush_scale) {
    Camera c;
    c.orthographic = true;
    glm::vec3 a, b, da, db;
    c.screenRay(100, 200, 800, 600, a, da);
    c.screenRay(700, 400, 800, 600, b, db);
    CHECK(near(da, db));
    CHECK(glm::length(a - b) > 0.1f);
    CHECK(std::abs(c.worldPerPixel(glm::vec3(0), 600) - c.worldPerPixel(glm::vec3(0, 0, -10), 600)) < 1e-6f);
    c.orthographic = false;
    c.screenRay(100, 200, 800, 600, a, da);
    c.screenRay(700, 400, 800, 600, b, db);
    CHECK(glm::length(da - db) > .1f);
}

TEST(camera_projection_toggle_preserves_target_plane_scale_and_lens) {
    Camera c;
    c.aspect = 16.0f / 9.0f;
    c.setFocalLengthMm(50);
    CHECK(std::abs(c.focalLengthMm() - 50) < 1e-4f);
    CHECK(std::abs(glm::degrees(c.fovY) - 26.9915f) < .001f);
    const glm::vec4 point(.4f, .5f, 0, 1);
    glm::vec4 perspective = c.projection() * c.view() * point;
    perspective /= perspective.w;
    c.orthographic = true;
    glm::vec4 ortho = c.projection() * c.view() * point;
    ortho /= ortho.w;
    CHECK(glm::length(glm::vec2(ortho) - glm::vec2(perspective)) < 1e-5f);
    AABB box; box.expand(glm::vec3(-1)); box.expand(glm::vec3(1));
    c.aspect = .5f;
    c.frame(box);
    for (float x : {-1.0f, 1.0f}) for (float y : {-1.0f, 1.0f}) {
        const glm::vec4 projected = c.projection() * c.view() * glm::vec4(x, y, 0, 1);
        CHECK(std::abs(projected.x / projected.w) < 1);
        CHECK(std::abs(projected.y / projected.w) < 1);
    }
}
