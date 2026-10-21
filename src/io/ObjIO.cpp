// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ObjIO.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

const char* skipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

} // namespace

bool loadObj(const std::string& path, Mesh& out, std::string& error) {
    std::ifstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot open " + path;
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Mesh m;
    std::vector<uint32_t> poly;
    const char* p = data.c_str();
    const char* end = p + data.size();
    size_t lineNo = 0;

    while (p < end) {
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
        ++lineNo;
        std::string line(p, lineEnd); // null-terminated copy for strtof/strtol
        p = lineEnd + 1;

        const char* s = skipSpace(line.c_str(), line.c_str() + line.size());
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            char* cur = const_cast<char*>(s + 1);
            glm::vec3 v;
            for (int i = 0; i < 3; ++i) {
                char* next;
                v[i] = std::strtof(cur, &next);
                if (next == cur) {
                    error = "Malformed vertex on line " + std::to_string(lineNo);
                    return false;
                }
                cur = next;
            }
            // Optional "v x y z r g b" vertex colours (MeshLab, scanners), 0..1 display RGB.
            glm::vec3 rgb;
            int channels = 0;
            for (; channels < 3; ++channels) {
                char* next;
                rgb[channels] = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;
            }
            if (channels == 3) {
                if (m.colors.size() < m.positions.size()) m.colors.resize(m.positions.size(), glm::vec3(1.0f));
                m.colors.push_back(srgbToLinear(rgb));
            } else if (!m.colors.empty()) {
                m.colors.push_back(glm::vec3(1.0f));
            }
            m.positions.push_back(v);
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            poly.clear();
            char* cur = const_cast<char*>(s + 1);
            while (true) {
                char* next;
                long idx = std::strtol(cur, &next, 10);
                if (next == cur) break;
                cur = next;
                while (*cur && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur; // skip /vt/vn
                long n = static_cast<long>(m.positions.size());
                long resolved = idx > 0 ? idx - 1 : n + idx;
                if (idx == 0 || resolved < 0 || resolved >= n) {
                    error = "Invalid face index on line " + std::to_string(lineNo);
                    return false;
                }
                poly.push_back(static_cast<uint32_t>(resolved));
            }
            for (size_t i = 2; i < poly.size(); ++i) m.triangles.push_back({poly[0], poly[i - 1], poly[i]});
        }
    }

    if (m.triangles.empty()) {
        error = "No faces found in " + path;
        return false;
    }
    if (!m.colors.empty()) m.colors.resize(m.positions.size(), glm::vec3(1.0f));
    m.rebuildTopology();
    m.recomputeAllNormals();
    out = std::move(m);
    return true;
}

bool saveObj(const std::string& path, const Mesh& mesh, std::string& error) {
    std::ofstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot write " + path;
        return false;
    }
    std::string buf;
    buf.reserve(mesh.vertexCount() * 40 + mesh.triangleCount() * 30);
    buf += "# Exported by SculptForge\n";
    char line[128];
    const bool colors = mesh.colors.size() == mesh.positions.size() && mesh.hasColors();
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const glm::vec3& v = mesh.positions[i];
        int n;
        if (colors) {
            const glm::vec3 c = linearToSrgb(mesh.colors[i]);
            n = std::snprintf(line, sizeof line, "v %.7g %.7g %.7g %.4f %.4f %.4f\n", v.x, v.y, v.z, c.x, c.y, c.z);
        } else {
            n = std::snprintf(line, sizeof line, "v %.7g %.7g %.7g\n", v.x, v.y, v.z);
        }
        buf.append(line, n);
    }
    for (const auto& t : mesh.triangles) {
        int n = std::snprintf(line, sizeof line, "f %u %u %u\n", t.x + 1, t.y + 1, t.z + 1);
        buf.append(line, n);
    }
    file.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!file) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

} // namespace sf
