// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Renderer.h"

#include "core/Matcap.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>
#include <vector>

namespace sf {

namespace {

const char* kMeshVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aCurvature;
layout(location = 3) in vec3 aColor; // polypaint (constant white when the mesh has none)
layout(location = 4) in float aMask; // constant 0 when the mesh has no mask
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vNormal;
out vec3 vPos;
out float vCurvature;
out vec3 vColor;
out float vMask;
void main() {
    vec4 p = uView * vec4(aPos, 1.0);
    vPos = p.xyz;
    vCurvature = aCurvature;
    vColor = aColor;
    vMask = aMask;
    vNormal = mat3(uView) * aNormal;
    gl_Position = uProj * p;
}
)";

// Studio "clay" shading: wrapped key light, cool fill, rim, soft spec, and curvature-based
// cavity/ridge shading so sculpted detail reads at a glance.
const char* kMeshFS = R"(#version 330 core
in vec3 vNormal;
in vec3 vPos;
in float vCurvature;
in vec3 vColor;
in float vMask;
uniform vec3 uBaseColor;
uniform int uPaint;      // 1: vertex colours replace the base colour
uniform int uShowMask;   // 1: darken masked areas
uniform int uGroups;     // 0: off, 1: per-triangle ids from uGroupIds, 2: everything is group 0
uniform usamplerBuffer uGroupIds;
uniform int uMatcap;          // 1: shade with the matcap texture instead of the studio lights
uniform sampler2D uMatcapTex; // sRGB-encoded lit sphere (core/Matcap.h)
out vec4 fragColor;
// Keep in sync with sf::polygroupColor.
vec3 groupColor(uint id) {
    uint h = id * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    float hue = float(h & 0xFFFFu) / 65536.0;
    float sat = 0.45 + 0.25 * float((h >> 16) & 0xFFu) / 255.0;
    float val = 0.70 + 0.25 * float((h >> 24) & 0xFFu) / 255.0;
    vec3 rgb = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return val * mix(vec3(1.0), rgb, sat);
}
void main() {
    vec3 albedo = uBaseColor;
    vec3 tint = vec3(1.0); // multiplies a matcap: polypaint or polygroup colours
    if (uGroups == 1) albedo = tint = groupColor(texelFetch(uGroupIds, gl_PrimitiveID).r);
    else if (uGroups == 2) albedo = tint = groupColor(0u);
    else if (uPaint == 1) albedo = tint = vColor;
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(-vPos);
    if (uMatcap == 1) {
        // View-aligned basis so the material stays put towards the edges of a perspective view.
        vec3 bx = normalize(vec3(v.z, 0.0, -v.x));
        vec3 by = cross(v, bx);
        vec2 uv = vec2(dot(bx, n), dot(by, n)) * 0.495 + 0.5;
        vec3 mc = pow(texture(uMatcapTex, uv).rgb, vec3(2.2));
        float mcrease = smoothstep(0.0, 0.16, vCurvature);
        float mridge = smoothstep(0.0, 0.2, -vCurvature);
        vec3 c = mc * tint * (1.0 - 0.55 * mcrease) * (1.0 + 0.2 * mridge);
        if (uShowMask == 1) c *= 1.0 - 0.75 * clamp(vMask, 0.0, 1.0);
        fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
        return;
    }
    vec3 key = normalize(vec3(0.45, 0.65, 0.6));
    vec3 fill = normalize(vec3(-0.7, -0.1, 0.4));
    float diff = max((dot(n, key) + 0.25) / 1.25, 0.0);
    float fillL = max(dot(n, fill), 0.0) * 0.25;
    float hemi = 0.5 + 0.5 * n.y;
    vec3 h = normalize(key + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * 0.18;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.3;
    float facing = smoothstep(-0.2, 0.6, dot(n, v));
    // vCurvature > 0 in creases, < 0 on ridges (see Renderer::updateCurvature).
    float crease = smoothstep(0.0, 0.16, vCurvature);
    float ridge = smoothstep(0.0, 0.2, -vCurvature);
    float cavity = 1.0 - 0.62 * crease;
    vec3 c = albedo * (0.12 * hemi + 0.95 * diff * diff + fillL * vec3(0.7, 0.8, 1.0)) * mix(0.7, 1.0, facing) * cavity;
    c *= mix(vec3(1.0), vec3(0.72, 0.55, 0.5), crease * 0.7); // warm, deeper colour in the cracks
    c += albedo * ridge * 0.3 * diff;                           // ridges catch the key light
    c += vec3(spec) * (1.0 - crease) + rim * vec3(0.8, 0.85, 1.0);
    if (uShowMask == 1) c *= 1.0 - 0.75 * clamp(vMask, 0.0, 1.0);
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
)";

const char* kWireFS = R"(#version 330 core
out vec4 fragColor;
void main() { fragColor = vec4(0.05, 0.05, 0.08, 0.35); }
)";

const char* kBgVS = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)";

const char* kBgFS = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 top = vec3(0.20, 0.21, 0.24);
    vec3 bottom = vec3(0.08, 0.08, 0.09);
    float d = length(vUv - vec2(0.5, 0.55));
    vec3 c = mix(bottom, top, clamp(vUv.y, 0.0, 1.0)) * (1.0 - 0.35 * d);
    fragColor = vec4(c, 1.0);
}
)";

// 3D Gaussian splats (EWA projection of each 3D covariance to a screen-space ellipse).
const char* kSplatVS = R"(#version 330 core
layout(location = 0) in uint aIndex;
uniform samplerBuffer uSplats;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uViewport;
uniform float uOpacity;
out vec4 vColor;
out vec2 vOffset;
const vec2 kCorners[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
void main() {
    int base = int(aIndex) * 4;
    vec4 t0 = texelFetch(uSplats, base);     // centre, opacity
    vec4 t1 = texelFetch(uSplats, base + 1); // covariance xx xy xz yy
    vec4 t2 = texelFetch(uSplats, base + 2); // covariance yz zz
    vec4 t3 = texelFetch(uSplats, base + 3); // colour
    vec4 cam = uView * vec4(t0.xyz, 1.0);
    vec4 clip = uProj * cam;
    float depth = -cam.z;
    if (depth <= 0.0 || abs(clip.x) > 1.3 * clip.w || abs(clip.y) > 1.3 * clip.w) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    mat3 sigma = mat3(t1.x, t1.y, t1.z, t1.y, t1.w, t2.x, t1.z, t2.x, t2.y);
    vec2 focal = vec2(uProj[0][0], uProj[1][1]) * uViewport * 0.5;
    mat3 J = mat3(focal.x / depth, 0.0, 0.0,
                  0.0, focal.y / depth, 0.0,
                  focal.x * cam.x / (depth * depth), focal.y * cam.y / (depth * depth), 0.0);
    mat3 T = J * mat3(uView);
    mat3 cov = T * sigma * transpose(T);
    float a = cov[0][0] + 0.3, b = cov[0][1], c = cov[1][1] + 0.3;
    float mid = 0.5 * (a + c);
    float rad = length(vec2(0.5 * (a - c), b));
    float l1 = mid + rad, l2 = max(mid - rad, 0.1);
    vec2 axis1 = abs(b) > 1e-6 ? normalize(vec2(b, l1 - a)) : (a >= c ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    vec2 axis2 = vec2(-axis1.y, axis1.x);
    vec2 corner = kCorners[gl_VertexID];
    vec2 px = corner.x * 3.0 * min(sqrt(l1), 1024.0) * axis1 + corner.y * 3.0 * min(sqrt(l2), 1024.0) * axis2;
    gl_Position = clip + vec4(px / uViewport * 2.0 * clip.w, 0.0, 0.0);
    vOffset = corner * 3.0;
    vColor = vec4(t3.rgb, t0.w * uOpacity);
}
)";

const char* kSplatFS = R"(#version 330 core
in vec4 vColor;
in vec2 vOffset;
out vec4 fragColor;
void main() {
    float alpha = min(vColor.a * exp(-0.5 * dot(vOffset, vOffset)), 0.99);
    if (alpha < 1.0 / 255.0) discard;
    fragColor = vec4(vColor.rgb, alpha);
}
)";

constexpr GLenum kTexture2 = 0x84C2; // GL_TEXTURE2 (matcap)

GLuint compile(GLenum type, const char* src, std::string& error) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("Shader compile error: ") + log;
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs, std::string& error) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, error);
    if (!v) return 0;
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, error);
    if (!f) {
        gl::DeleteShader(v);
        return 0;
    }
    GLuint p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    gl::LinkProgram(p);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    GLint ok = 0;
    gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetProgramInfoLog(p, sizeof log, nullptr, log);
        error = std::string("Shader link error: ") + log;
        gl::DeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

glm::vec3 polygroupColor(uint32_t id) {
    uint32_t h = id * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    const float hue = float(h & 0xFFFFu) / 65536.0f;
    const float sat = 0.45f + 0.25f * float((h >> 16) & 0xFFu) / 255.0f;
    const float val = 0.70f + 0.25f * float((h >> 24) & 0xFFu) / 255.0f;
    const glm::vec3 rgb = glm::clamp(glm::abs(glm::fract(hue + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f,
                                     0.0f, 1.0f);
    return val * glm::mix(glm::vec3(1.0f), rgb, sat);
}

bool Renderer::init(std::string& error) {
    meshProgram_ = link(kMeshVS, kMeshFS, error);
    if (!meshProgram_) return false;
    wireProgram_ = link(kMeshVS, kWireFS, error);
    if (!wireProgram_) return false;
    bgProgram_ = link(kBgVS, kBgFS, error);
    if (!bgProgram_) return false;
    splatProgram_ = link(kSplatVS, kSplatFS, error);
    if (!splatProgram_) return false;
    gl::GenVertexArrays(1, &splatVao_);
    gl::GenBuffers(1, &splatData_);
    gl::GenBuffers(1, &splatOrder_);
    gl::GenTextures(1, &splatTexture_);
    gl::BindVertexArray(splatVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, splatOrder_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), nullptr);
    gl::VertexAttribDivisor(0, 1);
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);

    gl::GenVertexArrays(1, &vao_);
    gl::GenVertexArrays(1, &emptyVao_);
    gl::GenBuffers(1, &posBuffer_);
    gl::GenBuffers(1, &nrmBuffer_);
    gl::GenBuffers(1, &indexBuffer_);

    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, posBuffer_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl::BindBuffer(GL_ARRAY_BUFFER, nrmBuffer_);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl::GenBuffers(1, &curvBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, curvBuffer_);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    // Colour (3) and mask (4) arrays are only enabled while the mesh has them.
    gl::GenBuffers(1, &colorBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, colorBuffer_);
    gl::VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl::GenBuffers(1, &maskBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, maskBuffer_);
    gl::VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::GenBuffers(1, &groupData_);
    gl::GenTextures(1, &groupTexture_);

    // Built-in matcaps, generated once (6 x 256 KB).
    matcapTextures_.assign(size_t(matcapCount()), 0);
    gl::GenTextures(GLsizei(matcapTextures_.size()), matcapTextures_.data());
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < matcapCount(); ++i) {
        constexpr int kSize = 256;
        const std::vector<uint8_t> pixels = generateMatcap(i, kSize);
        gl::BindTexture(GL_TEXTURE_2D, matcapTextures_[size_t(i)]);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    }
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return true;
}

GLuint Renderer::matcapTexture(int index) const {
    return index >= 0 && index < int(matcapTextures_.size()) ? matcapTextures_[size_t(index)] : 0;
}

void Renderer::shutdown() {
    gl::DeleteBuffers(1, &posBuffer_);
    gl::DeleteBuffers(1, &nrmBuffer_);
    gl::DeleteBuffers(1, &curvBuffer_);
    gl::DeleteBuffers(1, &colorBuffer_);
    gl::DeleteBuffers(1, &maskBuffer_);
    gl::DeleteBuffers(1, &groupData_);
    gl::DeleteTextures(1, &groupTexture_);
    if (!matcapTextures_.empty()) gl::DeleteTextures(GLsizei(matcapTextures_.size()), matcapTextures_.data());
    matcapTextures_.clear();
    gl::DeleteBuffers(1, &indexBuffer_);
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteVertexArrays(1, &emptyVao_);
    gl::DeleteProgram(meshProgram_);
    gl::DeleteProgram(wireProgram_);
    gl::DeleteProgram(bgProgram_);
    gl::DeleteProgram(splatProgram_);
    gl::DeleteBuffers(1, &splatData_);
    gl::DeleteBuffers(1, &splatOrder_);
    gl::DeleteTextures(1, &splatTexture_);
    gl::DeleteVertexArrays(1, &splatVao_);
}

void Renderer::syncSplats(const SplatCloud& c, uint64_t version) {
    if (version == splatVersion_) return;
    splatVersion_ = version;
    GLint maxTexels = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    const size_t n = std::min(c.size(), size_t(std::max(maxTexels, 0)) / 4);
    std::vector<glm::vec4> texels(n * 4);
    splatCenters_.resize(n);
    const glm::mat3 M(c.transform);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 p = c.worldPosition(i);
        const glm::mat3 A = M * glm::mat3_cast(c.rotations[i]) *
                            glm::mat3(glm::vec3(c.scales[i].x, 0, 0), glm::vec3(0, c.scales[i].y, 0), glm::vec3(0, 0, c.scales[i].z));
        const glm::mat3 S = A * glm::transpose(A);
        splatCenters_[i] = p;
        texels[i * 4 + 0] = glm::vec4(p, c.opacities[i]);
        texels[i * 4 + 1] = glm::vec4(S[0][0], S[0][1], S[0][2], S[1][1]);
        texels[i * 4 + 2] = glm::vec4(S[1][2], S[2][2], 0.0f, 0.0f);
        texels[i * 4 + 3] = glm::vec4(glm::clamp(c.color(i), glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
    }
    gl::BindBuffer(GL_TEXTURE_BUFFER, splatData_);
    gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(texels.size() * sizeof(glm::vec4)), texels.data(), GL_STATIC_DRAW);
    gl::BindTexture(GL_TEXTURE_BUFFER, splatTexture_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, splatData_);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    splatCount_ = GLsizei(n);
    lastSortView_ = glm::mat4(0.0f); // force a re-sort
}

void Renderer::drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity) {
    if (splatCount_ == 0) return;
    // Back-to-front order, re-sorted only when the camera moves (16-bit counting sort).
    if (view != lastSortView_) {
        lastSortView_ = view;
        const size_t n = size_t(splatCount_);
        std::vector<float> depth(n);
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        for (size_t i = 0; i < n; ++i) {
            const glm::vec3& p = splatCenters_[i];
            depth[i] = -(view[0][2] * p.x + view[1][2] * p.y + view[2][2] * p.z + view[3][2]);
            lo = std::min(lo, depth[i]);
            hi = std::max(hi, depth[i]);
        }
        const float scale = hi > lo ? 65535.0f / (hi - lo) : 0.0f;
        std::vector<uint32_t> count(65537, 0);
        std::vector<uint16_t> key(n);
        for (size_t i = 0; i < n; ++i) {
            key[i] = uint16_t(65535 - int((depth[i] - lo) * scale)); // far first
            ++count[size_t(key[i]) + 1];
        }
        for (size_t k = 1; k < count.size(); ++k) count[k] += count[k - 1];
        splatSorted_.resize(n);
        for (size_t i = 0; i < n; ++i) splatSorted_[count[key[i]]++] = uint32_t(i);
        gl::BindBuffer(GL_ARRAY_BUFFER, splatOrder_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * sizeof(uint32_t)), splatSorted_.data(), GL_STREAM_DRAW);
        gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    }
    gl::UseProgram(splatProgram_);
    gl::UniformMatrix4fv(gl::GetUniformLocation(splatProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
    gl::UniformMatrix4fv(gl::GetUniformLocation(splatProgram_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
    gl::Uniform2f(gl::GetUniformLocation(splatProgram_, "uViewport"), float(fbWidth), float(fbHeight));
    gl::Uniform1f(gl::GetUniformLocation(splatProgram_, "uOpacity"), opacity);
    gl::Uniform1i(gl::GetUniformLocation(splatProgram_, "uSplats"), 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_BUFFER, splatTexture_);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthMask(GL_FALSE);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::BindVertexArray(splatVao_);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, splatCount_);
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
    gl::DepthMask(GL_TRUE);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
}

// Signed curvature per vertex: how far the neighbour average sits above (crease, > 0) or below
// (ridge, < 0) the surface, relative to the local edge length.
void Renderer::updateCurvature(const Mesh& mesh, uint32_t begin, uint32_t end) {
    curvature_.resize(mesh.vertexCount(), 0.0f);
    for (uint32_t v = begin; v < end && v < mesh.vertexCount(); ++v) {
        const auto ring = mesh.neighbors(v);
        if (ring.empty()) {
            curvature_[v] = 0.0f;
            continue;
        }
        glm::vec3 avg(0.0f);
        float edge = 0.0f;
        const glm::vec3 p = mesh.positions[v];
        for (uint32_t n : ring) {
            avg += mesh.positions[n];
            edge += glm::distance(mesh.positions[n], p);
        }
        avg /= float(ring.size());
        edge /= float(ring.size());
        curvature_[v] = edge > 0.0f ? glm::dot(mesh.normals[v], avg - p) / edge : 0.0f;
    }
}

// Index buffer without hidden triangles, plus the polygroup id of every drawn
// triangle in draw order (looked up by gl_PrimitiveID).
void Renderer::uploadFaces(const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    const bool hidden = mesh.hidden.size() == nt && nt > 0;
    const bool groups = mesh.polygroups.size() == nt && nt > 0;
    const glm::uvec3* tris = mesh.triangles.data();
    size_t drawn = nt;
    visibleGroups_.clear();
    if (hidden) {
        visibleTris_.clear();
        for (size_t t = 0; t < nt; ++t) {
            if (mesh.hidden[t]) continue;
            visibleTris_.push_back(mesh.triangles[t]);
            if (groups) visibleGroups_.push_back(mesh.polygroups[t]);
        }
        tris = visibleTris_.data();
        drawn = visibleTris_.size();
    } else {
        std::vector<glm::uvec3>().swap(visibleTris_);
        if (groups) visibleGroups_ = mesh.polygroups;
    }
    gl::BindVertexArray(vao_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(drawn * sizeof(glm::uvec3)), tris, GL_STATIC_DRAW);
    gl::BindVertexArray(0);
    indexCount_ = GLsizei(drawn * 3);

    GLint maxTexels = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    hasGroups_ = groups && !visibleGroups_.empty() && visibleGroups_.size() <= size_t(std::max(maxTexels, 0));
    if (hasGroups_) {
        gl::BindBuffer(GL_TEXTURE_BUFFER, groupData_);
        gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(visibleGroups_.size() * sizeof(uint32_t)), visibleGroups_.data(),
                       GL_STATIC_DRAW);
        gl::BindTexture(GL_TEXTURE_BUFFER, groupTexture_);
        gl::TexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, groupData_);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    }
}

void Renderer::sync(Mesh& mesh) {
    const size_t nv = mesh.vertexCount();
    const bool full = mesh.topologyDirty || nv != vertexCapacity_;
    const bool ranged = !full && mesh.dirtyBegin < mesh.dirtyEnd && mesh.dirtyEnd <= nv;
    const GLintptr off3 = ranged ? GLintptr(mesh.dirtyBegin * sizeof(glm::vec3)) : 0;
    const GLsizeiptr size3 = ranged ? GLsizeiptr((mesh.dirtyEnd - mesh.dirtyBegin) * sizeof(glm::vec3)) : 0;
    const GLintptr off1 = ranged ? GLintptr(mesh.dirtyBegin * sizeof(float)) : 0;
    const GLsizeiptr size1 = ranged ? GLsizeiptr((mesh.dirtyEnd - mesh.dirtyBegin) * sizeof(float)) : 0;
    if (full) {
        updateCurvature(mesh, 0, uint32_t(nv));
        gl::BindBuffer(GL_ARRAY_BUFFER, posBuffer_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(nv * sizeof(glm::vec3)), mesh.positions.data(), GL_DYNAMIC_DRAW);
        gl::BindBuffer(GL_ARRAY_BUFFER, nrmBuffer_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(nv * sizeof(glm::vec3)), mesh.normals.data(), GL_DYNAMIC_DRAW);
        gl::BindBuffer(GL_ARRAY_BUFFER, curvBuffer_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(nv * sizeof(float)), curvature_.data(), GL_DYNAMIC_DRAW);
        uploadFaces(mesh);
        vertexCapacity_ = nv;
    } else if (ranged) {
        gl::BindBuffer(GL_ARRAY_BUFFER, posBuffer_);
        gl::BufferSubData(GL_ARRAY_BUFFER, off3, size3, mesh.positions.data() + mesh.dirtyBegin);
        gl::BindBuffer(GL_ARRAY_BUFFER, nrmBuffer_);
        gl::BufferSubData(GL_ARRAY_BUFFER, off3, size3, mesh.normals.data() + mesh.dirtyBegin);
        updateCurvature(mesh, mesh.dirtyBegin, mesh.dirtyEnd);
        gl::BindBuffer(GL_ARRAY_BUFFER, curvBuffer_);
        gl::BufferSubData(GL_ARRAY_BUFFER, off1, size1, curvature_.data() + mesh.dirtyBegin);
    }
    if (!full && mesh.facesDirty) uploadFaces(mesh);

    // Optional per-vertex attributes: a full upload when they appear (or the
    // mesh changed), the dirty range otherwise, and a disabled array when absent.
    auto syncAttribute = [&](GLuint buffer, GLuint location, bool& uploaded, const void* data, size_t elementSize,
                             bool present, GLintptr offset, GLsizeiptr size) {
        gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
        if (present && (full || !uploaded)) {
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(nv * elementSize), data, GL_DYNAMIC_DRAW);
        } else if (present && ranged) {
            gl::BufferSubData(GL_ARRAY_BUFFER, offset, size, static_cast<const uint8_t*>(data) + size_t(offset));
        } else if (!present && uploaded) {
            gl::BufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        }
        if (present != uploaded) {
            gl::BindVertexArray(vao_);
            if (present) gl::EnableVertexAttribArray(location);
            else gl::DisableVertexAttribArray(location);
            gl::BindVertexArray(0);
        }
        uploaded = present;
    };
    syncAttribute(colorBuffer_, 3, hasColors_, mesh.colors.data(), sizeof(glm::vec3), nv > 0 && mesh.colors.size() == nv,
                  off3, size3);
    syncAttribute(maskBuffer_, 4, hasMask_, mesh.mask.data(), sizeof(float), nv > 0 && mesh.mask.size() == nv, off1, size1);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    mesh.clearDirty();
}

bool Renderer::renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error) {
    GLuint fbo[2] = {}, rb[3] = {};
    gl::GenFramebuffers(2, fbo);
    gl::GenRenderbuffers(3, rb);
    // Multisampled colour + depth for drawing, plain colour for resolving.
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[0]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[1]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[2]);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[2]);
    ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
        draw(camera, width, height, settings);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
        gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
        std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 4);
        gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        rgba.resize(pixels.size());
        const size_t row = size_t(width) * 4;
        for (int y = 0; y < height; ++y) // GL rows are bottom-up
            std::copy_n(pixels.data() + size_t(height - 1 - y) * row, row, rgba.data() + size_t(y) * row);
    } else {
        error = "Offscreen framebuffer is not supported by this GPU/driver";
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(2, fbo);
    gl::DeleteRenderbuffers(3, rb);
    return ok;
}

bool Renderer::beginCapture(int width, int height, std::string& error) {
    if (width != captureW_ || height != captureH_) {
        if (captureFbo_[0]) {
            gl::DeleteFramebuffers(2, captureFbo_);
            gl::DeleteRenderbuffers(3, captureRb_);
        }
        gl::GenFramebuffers(2, captureFbo_);
        gl::GenRenderbuffers(3, captureRb_);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[0]);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[1]);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[2]);
        gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[0]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, captureRb_[0]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRb_[1]);
        bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[1]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, captureRb_[2]);
        ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!ok) {
            error = "Offscreen framebuffer is not supported by this GPU/driver";
            return false;
        }
        captureW_ = width;
        captureH_ = height;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[0]);
    return true;
}

void Renderer::endCapture(std::vector<uint8_t>& rgba) {
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, captureFbo_[0]);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFbo_[1]);
    gl::BlitFramebuffer(0, 0, captureW_, captureH_, 0, 0, captureW_, captureH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, captureFbo_[1]);
    rgba.resize(size_t(captureW_) * size_t(captureH_) * 4);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, captureW_, captureH_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // Flip to top-down rows for video encoders.
    const size_t row = size_t(captureW_) * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < captureH_ / 2; ++y) {
        uint8_t* a = rgba.data() + size_t(y) * row;
        uint8_t* b = rgba.data() + size_t(captureH_ - 1 - y) * row;
        std::copy_n(a, row, tmp.data());
        std::copy_n(b, row, a);
        std::copy_n(tmp.data(), row, b);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::draw(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings) {
    gl::Viewport(0, 0, fbWidth, fbHeight);
    gl::ClearColor(0.1f, 0.1f, 0.11f, 1.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::Enable(GL_MULTISAMPLE);

    // Background gradient.
    gl::Disable(GL_DEPTH_TEST);
    gl::UseProgram(bgProgram_);
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);

    glm::mat4 view = camera.view(), proj = camera.projection();
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LEQUAL);
    if (indexCount_ == 0 || !settings.showMesh) {
        if (settings.showSplats) drawSplats(view, proj, fbWidth, fbHeight, settings.splatOpacity);
        gl::UseProgram(0);
        return;
    }
    gl::BindVertexArray(vao_);

    auto setMatrices = [&](GLuint prog) {
        gl::UseProgram(prog);
        gl::UniformMatrix4fv(gl::GetUniformLocation(prog, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        gl::UniformMatrix4fv(gl::GetUniformLocation(prog, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
    };

    setMatrices(meshProgram_);
    gl::Uniform3fv(gl::GetUniformLocation(meshProgram_, "uBaseColor"), 1, glm::value_ptr(settings.baseColor));
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uPaint"), settings.showPolypaint && hasColors_ ? 1 : 0);
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uShowMask"), settings.showMask && hasMask_ ? 1 : 0);
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uGroups"), settings.showPolygroups ? (hasGroups_ ? 1 : 2) : 0);
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uGroupIds"), 1);
    const GLuint matcap = matcapTexture(settings.matcap);
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uMatcap"), matcap ? 1 : 0);
    gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uMatcapTex"), 2);
    gl::ActiveTexture(kTexture2);
    gl::BindTexture(GL_TEXTURE_2D, matcap);
    // Constant values for the attribute arrays that are disabled.
    if (!hasColors_) gl::VertexAttrib3f(3, 1.0f, 1.0f, 1.0f);
    if (!hasMask_) gl::VertexAttrib1f(4, 0.0f);
    gl::ActiveTexture(GL_TEXTURE1);
    gl::BindTexture(GL_TEXTURE_BUFFER, hasGroups_ ? groupTexture_ : 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::Enable(GL_POLYGON_OFFSET_FILL);
    gl::PolygonOffset(1.0f, 1.0f);
    gl::DrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    gl::Disable(GL_POLYGON_OFFSET_FILL);
    gl::ActiveTexture(GL_TEXTURE1);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    gl::ActiveTexture(kTexture2);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::ActiveTexture(GL_TEXTURE0);

    if (settings.wireframe) {
        setMatrices(wireProgram_);
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(GL_FALSE);
        gl::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        gl::DrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
        gl::PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        gl::DepthMask(GL_TRUE);
        gl::Disable(GL_BLEND);
    }
    gl::BindVertexArray(0);
    if (settings.showSplats) drawSplats(view, proj, fbWidth, fbHeight, settings.splatOpacity);
    gl::UseProgram(0);
}

} // namespace sf
