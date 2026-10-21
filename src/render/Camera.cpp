// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace sf {

glm::quat Camera::orientation() const {
    return glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
}

glm::vec3 Camera::position() const { return target + orientation() * glm::vec3(0, 0, distance); }

glm::vec3 Camera::forward() const { return orientation() * glm::vec3(0, 0, -1); }

glm::mat4 Camera::view() const {
    glm::mat4 world = glm::translate(glm::mat4(1.0f), position()) * glm::mat4_cast(orientation());
    return glm::inverse(world);
}

glm::mat4 Camera::projection() const { return glm::perspective(fovY, aspect, nearPlane(), farPlane()); }

void Camera::orbit(float dx, float dy) {
    yaw -= dx * 0.008f;
    pitch = std::clamp(pitch - dy * 0.008f, -glm::radians(89.0f), glm::radians(89.0f));
}

void Camera::pan(float dx, float dy, float viewportHeight) {
    float scale = 2.0f * distance * std::tan(fovY * 0.5f) / std::max(viewportHeight, 1.0f);
    glm::quat q = orientation();
    target += q * glm::vec3(-dx * scale, dy * scale, 0.0f);
}

void Camera::zoom(float steps) { distance = std::clamp(distance * std::pow(0.88f, steps), 1e-4f, 1e6f); }

void Camera::frame(const AABB& box) {
    if (!box.valid()) return;
    target = box.center();
    float radius = std::max(glm::length(box.extent()) * 0.5f, 1e-4f);
    distance = radius / std::sin(fovY * 0.5f) * 1.1f;
}

void Camera::screenRay(float x, float y, float width, float height, glm::vec3& origin, glm::vec3& dir) const {
    float nx = 2.0f * x / width - 1.0f;
    float ny = 1.0f - 2.0f * y / height;
    glm::mat4 inv = glm::inverse(projection() * view());
    glm::vec4 a = inv * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 b = inv * glm::vec4(nx, ny, 1.0f, 1.0f);
    origin = glm::vec3(a) / a.w;
    dir = glm::normalize(glm::vec3(b) / b.w - origin);
}

float Camera::worldPerPixel(const glm::vec3& point, float viewportHeight) const {
    float depth = std::max(glm::dot(point - position(), forward()), nearPlane());
    return 2.0f * depth * std::tan(fovY * 0.5f) / std::max(viewportHeight, 1.0f);
}

} // namespace sf
