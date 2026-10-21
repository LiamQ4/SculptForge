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
    bool showPolypaint = true;   // vertex colours replace baseColor when the mesh has them
    bool showMask = true;        // masked areas render darker (ZBrush style)
    bool showPolygroups = false; // colour each polygroup (see polygroupColor)
    int matcap = -1;             // built-in MatCap material (core/Matcap.h); -1 = studio clay lighting
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
};

// Display colour of a polygroup id (linear RGB): a stable hash spread over
// pleasant hues. The mesh shader uses the same formula.
glm::vec3 polygroupColor(uint32_t id);

// Mesh drawing honours the optional Mesh attributes: colours (polypaint), mask
// (darkening), polygroups (group colours) and hidden triangles (not drawn).
// Mark changed vertices dirty for colour/mask edits and set Mesh::facesDirty
// after changing `hidden` or `polygroups`.
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

    // Texture of built-in matcap `index` (sRGB RGBA8, rows bottom-up) for UI previews; 0 if none.
    GLuint matcapTexture(int index) const;

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
    void updateCurvature(const Mesh& mesh, uint32_t begin, uint32_t end);
    void uploadFaces(const Mesh& mesh);
    GLuint posBuffer_ = 0, nrmBuffer_ = 0, curvBuffer_ = 0, indexBuffer_ = 0;
    GLuint colorBuffer_ = 0, maskBuffer_ = 0;
    GLuint groupData_ = 0, groupTexture_ = 0; // polygroup id per drawn triangle (texture buffer)
    bool hasColors_ = false, hasMask_ = false, hasGroups_ = false;
    std::vector<float> curvature_;
    std::vector<glm::uvec3> visibleTris_;
    std::vector<uint32_t> visibleGroups_;
    GLsizei indexCount_ = 0;
    std::vector<GLuint> matcapTextures_; // one per built-in matcap (core/Matcap.h)
    size_t vertexCapacity_ = 0;
};

} // namespace sf
