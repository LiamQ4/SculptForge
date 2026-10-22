// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ObjIO.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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

// True for a "<c> ..." or bare "<c>" statement.
bool isStatement(const char* s, char c) { return s[0] == c && (s[1] == ' ' || s[1] == '\t' || s[1] == '\r' || s[1] == 0); }

// The text after a one-letter statement, without surrounding blanks.
std::string statementName(const char* s) {
    std::string name(s + 1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r')) name.pop_back();
    size_t start = 0;
    while (start < name.size() && (name[start] == ' ' || name[start] == '\t')) ++start;
    return name.substr(start);
}

// One `o` object while parsing: triangles index the file's global vertex list.
struct ObjObject {
    std::string name;
    std::vector<glm::uvec3> triangles;
    std::vector<uint32_t> groups;          // per triangle: index into groupNames
    std::vector<std::string> groupNames;   // `g` names in order of first use ("" = before any `g`)
    std::map<std::string, uint32_t> groupIndex;
    std::string pendingGroup;  // the last `g`, registered by its first face (unused names don't count)
    uint32_t currentGroup = ~0u;
};

// Polygroups from `g` names: only when the object uses two or more (ids 1, 2, ... in order of first use).
void applyGroups(const ObjObject& o, Mesh& m) {
    if (o.groupNames.size() < 2) return;
    m.polygroups.resize(o.groups.size());
    for (size_t t = 0; t < o.groups.size(); ++t) m.polygroups[t] = o.groups[t] + 1;
}

bool parseObj(const std::string& path, bool splitObjects, std::vector<MeshObject>& out, std::string& error) {
    std::ifstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot open " + path;
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<glm::vec3> positions, colors; // colours: empty until a vertex has one
    std::vector<uint8_t> colored;             // per vertex (while colours exist): had an explicit colour
    std::vector<ObjObject> objects(1);
    std::vector<uint32_t> poly;
    const char* p = data.c_str();
    const char* end = p + data.size();
    size_t lineNo = 0;

    auto groupFor = [](ObjObject& o, const std::string& name) {
        auto [it, inserted] = o.groupIndex.try_emplace(name, uint32_t(o.groupNames.size()));
        if (inserted) o.groupNames.push_back(name);
        o.currentGroup = it->second;
    };

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
                if (colors.size() < positions.size()) {
                    colors.resize(positions.size(), glm::vec3(1.0f));
                    colored.resize(positions.size(), 0);
                }
                colors.push_back(srgbToLinear(rgb));
                colored.push_back(1);
            } else if (!colors.empty()) {
                colors.push_back(glm::vec3(1.0f));
                colored.push_back(0);
            }
            positions.push_back(v);
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            poly.clear();
            char* cur = const_cast<char*>(s + 1);
            while (true) {
                char* next;
                long idx = std::strtol(cur, &next, 10);
                if (next == cur) break;
                cur = next;
                while (*cur && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur; // skip /vt/vn
                long n = static_cast<long>(positions.size());
                long resolved = idx > 0 ? idx - 1 : n + idx;
                if (idx == 0 || resolved < 0 || resolved >= n) {
                    error = "Invalid face index on line " + std::to_string(lineNo);
                    return false;
                }
                poly.push_back(static_cast<uint32_t>(resolved));
            }
            ObjObject& o = objects.back();
            if (poly.size() >= 3 && o.currentGroup == ~0u) groupFor(o, o.pendingGroup);
            for (size_t i = 2; i < poly.size(); ++i) {
                o.triangles.push_back({poly[0], poly[i - 1], poly[i]});
                o.groups.push_back(o.currentGroup);
            }
        } else if (isStatement(s, 'g')) {
            objects.back().pendingGroup = statementName(s);
            objects.back().currentGroup = ~0u;
        } else if (isStatement(s, 'o') && splitObjects) {
            // A new object; an `o` before any faces just names the current one.
            if (!objects.back().triangles.empty()) objects.emplace_back();
            objects.back() = ObjObject{};
            objects.back().name = statementName(s);
        }
    }

    size_t withFaces = 0;
    for (const auto& o : objects) withFaces += !o.triangles.empty();
    if (withFaces == 0) {
        error = "No faces found in " + path;
        return false;
    }
    if (!colors.empty()) {
        colors.resize(positions.size(), glm::vec3(1.0f));
        colored.resize(positions.size(), 0);
    }
    out.clear();
    if (withFaces == 1) {
        // One object: every vertex is kept, as stored.
        for (auto& o : objects) {
            if (o.triangles.empty()) continue;
            MeshObject obj;
            obj.name = o.name;
            obj.mesh.positions = std::move(positions);
            obj.mesh.colors = std::move(colors);
            obj.mesh.triangles = std::move(o.triangles);
            applyGroups(o, obj.mesh);
            out.push_back(std::move(obj));
        }
    } else {
        // Several: each object gets the vertices its faces use.
        std::vector<uint32_t> local(positions.size(), ~0u);
        for (auto& o : objects) {
            if (o.triangles.empty()) continue;
            MeshObject obj;
            obj.name = o.name;
            Mesh& m = obj.mesh;
            // Vertices keep their file order (so an object written as one block reads back unchanged).
            std::vector<uint32_t> used;
            for (const auto& t : o.triangles)
                for (int k = 0; k < 3; ++k)
                    if (local[t[k]] == ~0u) {
                        local[t[k]] = 0;
                        used.push_back(t[k]);
                    }
            std::sort(used.begin(), used.end());
            bool anyColor = false;
            for (size_t i = 0; i < used.size(); ++i) {
                local[used[i]] = uint32_t(i);
                anyColor |= !colored.empty() && colored[used[i]];
            }
            for (auto& t : o.triangles)
                for (int k = 0; k < 3; ++k) t[k] = local[t[k]];
            m.positions.reserve(used.size());
            for (uint32_t v : used) m.positions.push_back(positions[v]);
            if (anyColor)
                for (uint32_t v : used) m.colors.push_back(colors[v]);
            for (uint32_t v : used) local[v] = ~0u;
            m.triangles = std::move(o.triangles);
            applyGroups(o, m);
            out.push_back(std::move(obj));
        }
    }
    for (auto& obj : out) {
        obj.mesh.rebuildTopology();
        obj.mesh.recomputeAllNormals();
    }
    return true;
}

} // namespace

bool loadObj(const std::string& path, Mesh& out, std::string& error) {
    std::vector<MeshObject> objects;
    if (!parseObj(path, false, objects, error)) return false;
    out = std::move(objects.front().mesh);
    return true;
}

bool loadObj(const std::string& path, std::vector<MeshObject>& out, std::string& error) {
    return parseObj(path, true, out, error);
}

bool saveObj(const std::string& path, const Mesh& mesh, std::string& error) {
    return saveObj(path, std::vector<MeshObjectRef>{{"", &mesh}}, error);
}

bool saveObj(const std::string& path, const std::vector<MeshObjectRef>& objects, std::string& error) {
    if (objects.empty()) {
        error = "Nothing to export";
        return false;
    }
    std::ofstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot write " + path;
        return false;
    }
    std::string buf;
    size_t reserve = 0;
    for (const auto& o : objects) reserve += o.mesh->vertexCount() * 40 + o.mesh->triangleCount() * 30;
    buf.reserve(reserve);
    buf += "# Exported by SculptForge\n";
    char line[128];
    uint32_t first = 1; // OBJ indices are 1-based and global across objects
    for (const auto& o : objects) {
        const Mesh& mesh = *o.mesh;
        if (objects.size() > 1) {
            std::string name = o.name.empty() ? std::string("Object") : o.name;
            for (char& c : name)
                if (c == '\n' || c == '\r') c = ' ';
            buf += "o " + name + "\n";
        }
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
        // Polygroups as `g` groups (ZBrush's OBJ convention), when there is more than one.
        bool groups = false;
        for (uint32_t t = 1; t < mesh.triangleCount() && !groups; ++t) groups = mesh.polygroupAt(t) != mesh.polygroupAt(0);
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            if (groups && (t == 0 || mesh.polygroupAt(t) != mesh.polygroupAt(t - 1))) {
                const int n = std::snprintf(line, sizeof line, "g group%u\n", mesh.polygroupAt(t));
                buf.append(line, n);
            }
            const glm::uvec3& tri = mesh.triangles[t];
            const int n = std::snprintf(line, sizeof line, "f %u %u %u\n", tri.x + first, tri.y + first, tri.z + first);
            buf.append(line, n);
        }
        first += uint32_t(mesh.vertexCount());
    }
    file.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!file) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

} // namespace sf
