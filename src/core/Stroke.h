// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stroke types: how cursor motion turns into brush dabs. Screen-space and
// UI-independent so they can be tested without a window.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sf {

enum class StrokeType {
    Dots,      // evenly spaced dabs along the cursor path
    DragRect,  // click-drag stamps one dab: drag length = size, drag direction = alpha rotation
    DragDot,   // click-drag slides one dab of the brush's Draw Size over the surface until release
    Spray,     // scattered dabs with random size, strength and rotation
    LazyMouse, // dabs follow a point dragged behind the cursor on a string (smooth lines)
    Count
};

const char* strokeTypeName(StrokeType type);

// Points at even `spacing` along a polyline, carrying the leftover distance between calls.
class StrokeSpacer {
public:
    void reset(glm::vec2 start);
    // Appends the dab positions between the last point and `to`.
    void moveTo(glm::vec2 to, float spacing, std::vector<glm::vec2>& out);
    glm::vec2 last() const { return last_; }

private:
    glm::vec2 last_{0.0f};
    float residual_ = 0.0f;
};

// ZBrush-style lazy mouse: the brush point trails the cursor by `radius`
// pixels and only moves once the string is taut, filtering out hand jitter.
struct LazyMouse {
    float radius = 40.0f;
    glm::vec2 point{0.0f};

    void reset(glm::vec2 p) { point = p; }
    // Pulls the brush point toward `cursor`; returns its new position.
    glm::vec2 follow(glm::vec2 cursor);
};

// One sprayed dab, relative to the path point.
struct SprayDab {
    glm::vec2 offset{0.0f}; // pixels
    float scale = 1.0f;     // brush radius multiplier
    float strength = 1.0f;  // strength multiplier
    float angle = 0.0f;     // alpha rotation, radians
};

// Deterministic random scatter (xorshift), so a seed always gives the same spray.
class Spray {
public:
    explicit Spray(uint32_t seed = 1) { reseed(seed); }
    void reseed(uint32_t seed) { state_ = seed ? seed : 1u; }
    float uniform(); // 0..1
    // Offset within `scatter` x radius (uniform over the disc), scale and strength in 0.5..1.
    SprayDab next(float radiusPx, float scatter);

private:
    uint32_t state_ = 1;
};

} // namespace sf
