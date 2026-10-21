// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"
#include "core/Splats.h"
#include "render/Camera.h"
#include "render/GL.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct RenderSettings {
    bool wireframe = false;
    glm::vec3 baseColor{0.80f, 0.55f, 0.45f}; // clay
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
};

class Renderer {
public:
    bool init(std::string& error);
    void shutdown();
    // Uploads whatever the mesh marked dirty, then clears its dirty state.
    void sync(Mesh& mesh);
    // Re-uploads the splats when `version` differs from the last upload.
    void syncSplats(const SplatCloud& splats, uint64_t version);
    void draw(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings);
    // Offscreen render (4x MSAA) into top-down RGBA8 pixels.
    bool renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                       std::vector<uint8_t>& rgba, std::string& error);

    // Persistent offscreen target (4x MSAA) for recording: draw between these calls.
    bool beginCapture(int width, int height, std::string& error);
    void endCapture(std::vector<uint8_t>& rgba); // top-down RGBA rows

private:
    void drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity);

    GLuint meshProgram_ = 0, wireProgram_ = 0, bgProgram_ = 0, splatProgram_ = 0;
    // Splats: per-splat data in a texture buffer, drawn as instanced quads in back-to-front order.
    GLuint splatVao_ = 0, splatData_ = 0, splatTexture_ = 0, splatOrder_ = 0;
    GLsizei splatCount_ = 0;
    uint64_t splatVersion_ = ~uint64_t(0);
    std::vector<glm::vec3> splatCenters_;
    std::vector<uint32_t> splatSorted_;
    glm::mat4 lastSortView_{0.0f};
    GLuint captureFbo_[2] = {}, captureRb_[3] = {};
    int captureW_ = 0, captureH_ = 0;
    GLuint vao_ = 0, emptyVao_ = 0;
    GLuint posBuffer_ = 0, nrmBuffer_ = 0, indexBuffer_ = 0;
    GLsizei indexCount_ = 0;
    size_t vertexCapacity_ = 0;
};

} // namespace sf
