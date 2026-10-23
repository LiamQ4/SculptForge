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
    bool showTexture = true;     // with showPolypaint: a SubTool's painted texture (core/Texture.h) multiplies in
    bool showMask = true;        // masked areas render darker (ZBrush style)
    bool showPolygroups = false; // colour each polygroup (see polygroupColor)
    int matcap = 0;              // built-in MatCap material (core/Matcap.h), 0 = Sculpt; -1 = studio clay lighting
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
    bool dimInactive = true;   // SubTools other than the active one render slightly darker (ZBrush style)
};

// One mesh of the scene for Renderer::syncScene: a SubTool, keyed by its stable id.
struct SceneMesh {
    uint32_t id = 0;
    Mesh* mesh = nullptr;
    bool active = false; // gets mask darkening, polygroup colours and wireframe; never dimmed
    bool visible = true; // hidden meshes keep their GPU slot but aren't drawn (or uploaded)
};

// Part of the target the 3D view is drawn into (framebuffer pixels, origin bottom-left); the rest is
// cleared to `surround`. w == 0: the whole target.
struct RenderViewport {
    int x = 0, y = 0, w = 0, h = 0;
    glm::vec3 surround{0.118f, 0.118f, 0.125f};
};

// Display colour of a polygroup id (linear RGB): a stable hash spread over
// pleasant hues. The mesh shader uses the same formula.
glm::vec3 polygroupColor(uint32_t id);

// Draws every visible SubTool, each from its own GPU buffers (a slot keyed by
// SubTool id). Mesh drawing honours the optional Mesh attributes: colours
// (polypaint), mask (darkening), polygroups (group colours) and hidden
// triangles (not drawn). Mark changed vertices dirty for colour/mask edits and
// set Mesh::facesDirty after changing `hidden` or `polygroups`. Only what a
// mesh marks dirty is uploaded, so inactive SubTools upload once.
class Renderer {
public:
    bool init(std::string& error);
    void shutdown();
    // Makes `scene` what draw() shows, in order: uploads whatever each visible
    // mesh marked dirty and clears its dirty state (a new slot uploads
    // everything), and frees the slots of ids no longer listed.
    void syncScene(const std::vector<SceneMesh>& scene);
    // Re-uploads the splats when `version` differs from the last upload.
    void syncSplats(const SplatCloud& splats, uint64_t version);
    using Viewport = RenderViewport;
    void draw(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings, const Viewport& viewport = {});
    // Offscreen render (4x MSAA) into top-down RGBA8 pixels.
    bool renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                       std::vector<uint8_t>& rgba, std::string& error);

    // Persistent offscreen target (4x MSAA) for recording: draw between these calls.
    bool beginCapture(int width, int height, std::string& error);
    void endCapture(std::vector<uint8_t>& rgba); // top-down RGBA rows

    // GL texture of the painted texture of SubTool `id` as last synced (0 if none), for UI previews (rows bottom-up).
    GLuint paintTexture(uint32_t id) const;

    // Texture of built-in matcap `index` (sRGB RGBA8, rows bottom-up) for UI previews; 0 if none.
    GLuint matcapTexture(int index) const;

    // --- Thumbnails (LightBox / pickers) ---
    // Renders `mesh` alone (not the synced scene, which is left as it was) into a `size` x `size`
    // top-down RGBA8 image with a transparent background. Keeps the caller's framebuffer and viewport.
    bool renderMeshThumbnail(Mesh& mesh, const Camera& camera, int size, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error);

private:
    void drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity);

    GLuint meshProgram_ = 0, meshTexProgram_ = 0, wireProgram_ = 0, bgProgram_ = 0, splatProgram_ = 0;
    // Splats: per-splat data in a texture buffer, drawn as instanced quads in back-to-front order.
    GLuint splatVao_ = 0, splatData_ = 0, splatTexture_ = 0, splatOrder_ = 0;
    GLsizei splatCount_ = 0;
    uint64_t splatVersion_ = ~uint64_t(0);
    std::vector<glm::vec3> splatCenters_;
    std::vector<uint32_t> splatSorted_;
    glm::mat4 lastSortView_{0.0f};
    GLuint captureFbo_[2] = {}, captureRb_[3] = {};
    int captureW_ = 0, captureH_ = 0;
    GLuint emptyVao_ = 0;
    bool thumbnailPass_ = false; // draw(): transparent background, no gradient (renderMeshThumbnail)
    // The GPU copy of one mesh (one SubTool).
    struct MeshSlot {
        uint32_t id = 0;
        GLuint vao = 0;
        GLuint posBuffer = 0, nrmBuffer = 0, curvBuffer = 0, indexBuffer = 0;
        GLuint colorBuffer = 0, maskBuffer = 0;
        GLuint groupData = 0, groupTexture = 0; // polygroup id per drawn triangle (texture buffer)
        GLuint uvData = 0, uvTexture = 0; // per drawn triangle corner UVs (texture buffer), meshes with a texture
        GLuint paintTexture = 0;          // the painted texture (sRGB, mipmapped)
        uint64_t textureSerial = 0, textureRevision = 0;
        int textureW = 0, textureH = 0;
        bool hasColors = false, hasMask = false, hasGroups = false, hasUvs = false;
        bool fresh = true; // nothing uploaded yet
        std::vector<float> curvature;
        GLsizei indexCount = 0;
        GLsizei lineCount = 0; // wireframe edge indices after the triangles (meshes with quads; 0 = draw triangles)
        size_t vertexCapacity = 0;
    };
    MeshSlot& slot(uint32_t id); // created on first use
    static void freeSlot(MeshSlot& s);
    void sync(MeshSlot& s, Mesh& mesh);
    static void updateCurvature(MeshSlot& s, const Mesh& mesh, uint32_t begin, uint32_t end);
    void uploadFaces(MeshSlot& s, const Mesh& mesh);
    void syncTexture(MeshSlot& s, Mesh& mesh);
    void drawSlot(const MeshSlot& s, bool active, const RenderSettings& settings);
    std::vector<MeshSlot> slots_;
    struct DrawItem {
        uint32_t id;
        bool active;
    };
    std::vector<DrawItem> drawList_;      // visible meshes, in scene order
    std::vector<glm::uvec3> visibleTris_; // scratch for uploadFaces
    std::vector<uint32_t> visibleGroups_;
    std::vector<glm::vec2> visibleUvs_; // scratch for uploadFaces
    std::vector<uint32_t> wireLines_; // scratch for uploadFaces: wireframe edges of meshes with quads
    std::vector<GLuint> matcapTextures_; // one per built-in matcap (core/Matcap.h)
};

} // namespace sf
