// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tablet settings: the global pressure curve, pen button / eraser mappings
// and their JSON round trip, and the stroke-start pressure guard.
#include "TestHarness.h"

#include "core/Tablet.h"

#include <cmath>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
} // namespace

TEST(pressure_curve_linear_is_identity) {
    const PressureCurve c;
    CHECK(c.isLinear());
    for (float p : {0.0f, 0.1f, 0.5f, 0.77f, 1.0f}) CHECK(near(c.apply(p), p));
    CHECK(near(c.apply(-1.0f), 0.0f)); // clamped
    CHECK(near(c.apply(2.0f), 1.0f));
    CHECK(near(c.apply(NAN), 0.0f));
}

TEST(pressure_curve_soft_and_firm) {
    const PressureCurve soft = PressureCurve::soft(), firm = PressureCurve::firm();
    CHECK(!soft.isLinear());
    CHECK(!firm.isLinear());
    // Same ends, different middle: soft gives more for a light touch, firm needs a harder press.
    for (const PressureCurve& c : {soft, firm}) {
        CHECK(near(c.apply(0.0f), 0.0f));
        CHECK(near(c.apply(1.0f), 1.0f));
    }
    CHECK(soft.apply(0.3f) > 0.3f);
    CHECK(firm.apply(0.3f) < 0.3f);
    CHECK(near(firm.apply(0.5f), std::pow(0.5f, firm.gamma)));
    // Monotonic.
    float last = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v = firm.apply(float(i) / 20.0f);
        CHECK(v >= last);
        last = v;
    }
}

TEST(pressure_curve_min_max_threshold) {
    PressureCurve c;
    c.minOut = 0.2f;
    c.maxOut = 0.8f;
    CHECK(near(c.apply(0.0f), 0.2f));
    CHECK(near(c.apply(1.0f), 0.8f));
    CHECK(near(c.apply(0.5f), 0.5f));
    c = PressureCurve();
    c.threshold = 0.1f;
    c.fullAt = 0.6f;
    CHECK(near(c.apply(0.05f), 0.0f)); // below the threshold: the lightest touch
    CHECK(near(c.apply(0.35f), 0.5f)); // halfway between threshold and full
    CHECK(near(c.apply(0.7f), 1.0f));  // a heavy hand reaches full pressure sooner
    c.fullAt = c.threshold;            // degenerate range must not divide by zero
    CHECK(std::isfinite(c.apply(0.1f)));
    c = PressureCurve();
    c.gamma = 100.0f; // out of range: clamped to 4
    CHECK(near(c.apply(0.5f), std::pow(0.5f, 4.0f)));
}

TEST(tablet_settings_json_round_trip) {
    TabletSettings t;
    CHECK(t.eraser == PenAction::Invert);
    CHECK(t.buttons[0] == PenAction::RightClick);
    CHECK(t.buttons[1] == PenAction::Invert);
    t.curve = PressureCurve::firm();
    t.curve.minOut = 0.1f;
    t.eraser = PenAction::EraseMask;
    t.buttons = {PenAction::RotateView, PenAction::Smooth, PenAction::None};
    TabletSettings back;
    fromJson(toJson(t), back);
    CHECK(near(back.curve.gamma, t.curve.gamma));
    CHECK(near(back.curve.minOut, 0.1f));
    CHECK(back.eraser == PenAction::EraseMask);
    CHECK(back.buttons[0] == PenAction::RotateView);
    CHECK(back.buttons[1] == PenAction::Smooth);
    CHECK(back.buttons[2] == PenAction::None);

    // Unknown or unsuitable values keep the defaults.
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"eraser":"pan","buttons":["erase_mask","bogus"],"pressureCurve":{"gamma":-3}})", j, err));
    TabletSettings d;
    fromJson(j, d);
    CHECK(d.eraser == PenAction::Invert);
    CHECK(d.buttons[0] == PenAction::RightClick);
    CHECK(d.buttons[1] == PenAction::Invert);
    CHECK(near(d.curve.gamma, 0.25f));
    CHECK(std::string(penActionLabel(PenAction::RightClick)) == "Right-click");
}

TEST(pen_pressure_tracker_guards_stroke_start) {
    PenPressureTracker t;
    t.touchDown();
    CHECK(!t.readyForFirstDab()); // the first touch after start: 1.0 is only assumed
    CHECK(!t.everSampled());      // (the app draws at the first motion if no sample ever comes)
    CHECK(near(t.raw(), 1.0f));
    t.sample(0.9f); // end of the previous stroke
    t.touchUp();
    t.touchDown();
    CHECK(!t.readyForFirstDab()); // 0.9 is stale: wait for this stroke's own sample
    t.sample(0.0f);
    CHECK(!t.readyForFirstDab()); // touching but not pressing yet
    t.sample(0.15f);
    CHECK(t.readyForFirstDab());
    CHECK(near(t.raw(), 0.15f));
    t.sample(5.0f);
    CHECK(near(t.raw(), 1.0f));
}
