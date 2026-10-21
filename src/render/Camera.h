// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace sf {

// Turntable orbit camera.
class Camera {
public:
    glm::vec3 target{0.0f};
    float distance = 4.0f;
    float yaw = 0.0f;   // radians around world Y
    float pitch = 0.0f; // radians, clamped to avoid flipping
    float fovY = glm::radians(35.0f);
    float aspect = 1.0f;

    glm::quat orientation() const;
    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::mat4 view() const;
    glm::mat4 projection() const;

    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void zoom(float steps);
    void frame(const AABB& box);

    // Ray through a window coordinate (origin top-left).
    void screenRay(float x, float y, float width, float height, glm::vec3& origin, glm::vec3& dir) const;
    // World units covered by one window pixel at the depth of `point`.
    float worldPerPixel(const glm::vec3& point, float viewportHeight) const;

private:
    float nearPlane() const { return distance * 0.01f; }
    float farPlane() const { return distance * 100.0f; }
};

} // namespace sf
