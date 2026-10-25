// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/BlenderBridge.h"

#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "io/GozIO.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathUtf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string envValue(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
    std::string s = value;
    std::free(value);
    return s;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Writes through a temporary file and a rename, so a reader never sees half a file.
bool writeAtomically(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f || !f.write(text.data(), std::streamsize(text.size()))) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

std::string number(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A file name the manifest may name: no folders, nothing that leaves the folder.
bool plainFileName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos;
}

// Point (x, y, z) -> (x, z, -y) as a matrix (and its inverse, the transpose).
glm::dmat4 axesBlenderToSf() {
    glm::dmat4 a(0.0);
    a[0][0] = 1.0; // column 0: x -> x
    a[1][2] = -1.0; // column 1: y -> -z
    a[2][1] = 1.0; // column 2: z -> y
    a[3][3] = 1.0;
    return a;
}

} // namespace

std::string blenderExchangeRoot() {
    const std::string custom = envValue("SCULPTFORGE_BLENDER_EXCHANGE");
    if (!custom.empty()) return custom;
    const std::string base = appDataDir();
    return base.empty() ? std::string() : pathUtf8(utf8Path(base) / "blender");
}

std::string blenderInbox(const std::string& root) { return pathUtf8(utf8Path(root) / "to_sculptforge"); }
std::string blenderOutbox(const std::string& root) { return pathUtf8(utf8Path(root) / "to_blender"); }
std::string bridgeManifestPath(const std::string& folder) { return pathUtf8(utf8Path(folder) / "manifest.json"); }

glm::dvec3 blenderToSculptForge(const glm::dvec3& p) { return {p.x, p.z, -p.y}; }
glm::dvec3 sculptForgeToBlender(const glm::dvec3& p) { return {p.x, -p.z, p.y}; }
glm::dmat4 blenderMatrixToSculptForge(const glm::dmat4& m) {
    const glm::dmat4 a = axesBlenderToSf();
    return a * m * glm::transpose(a);
}

bool parseBridgeManifest(const std::string& text, BridgeManifest& out, std::string& error) {
    Json j;
    if (!Json::parse(text, j, error)) {
        error = "Bridge manifest is not valid JSON: " + error;
        return false;
    }
    if (!j.isObject() || j["format"].asString() != "sculptforge-bridge") {
        error = "Not a SculptForge bridge manifest";
        return false;
    }
    if (j["version"].asNumber(1) > 1) {
        error = "The bridge manifest is from a newer add-on or SculptForge (version " +
                std::to_string(int(j["version"].asNumber())) + "); update the older one";
        return false;
    }
    BridgeManifest m;
    m.revision = int64_t(j["revision"].asNumber(0));
    m.sender = j["sender"].asString();
    m.senderVersion = j["sender_version"].asString();
    for (const Json& o : j["objects"].items()) {
        BridgeObject b;
        b.name = o["name"].asString();
        b.file = o["file"].asString();
        if (!plainFileName(b.file)) {
            error = "Bridge manifest names a bad file: \"" + b.file + "\"";
            return false;
        }
        const Json& mw = o["matrix_world"];
        if (mw.isArray()) {
            // Rows, as Blender prints a Matrix: matrix_world[row][column].
            bool ok = mw.size() == 4;
            for (size_t r = 0; ok && r < 4; ++r) {
                ok = mw[r].isArray() && mw[r].size() == 4;
                for (size_t c = 0; ok && c < 4; ++c) {
                    ok = mw[r][c].isNumber() && std::isfinite(mw[r][c].asNumber());
                    if (ok) b.matrix[int(c)][int(r)] = mw[r][c].asNumber();
                }
            }
            if (!ok) {
                error = "Bridge manifest has a bad matrix_world for \"" + b.name + "\"";
                return false;
            }
            b.hasMatrix = true;
        }
        m.objects.push_back(std::move(b));
    }
    out = std::move(m);
    return true;
}

std::string writeBridgeManifest(const BridgeManifest& m) {
    std::string s = "{\n  \"format\": \"sculptforge-bridge\",\n  \"version\": 1,\n";
    s += "  \"revision\": " + std::to_string(m.revision) + ",\n";
    s += "  \"sender\": " + jsonQuote(m.sender) + ",\n";
    s += "  \"sender_version\": " + jsonQuote(m.senderVersion) + ",\n";
    s += "  \"time\": " + number(nowSeconds()) + ",\n";
    s += "  \"objects\": [";
    for (size_t i = 0; i < m.objects.size(); ++i) {
        const BridgeObject& o = m.objects[i];
        s += i ? ",\n    {" : "\n    {";
        s += "\"name\": " + jsonQuote(o.name) + ", \"file\": " + jsonQuote(o.file);
        if (o.hasMatrix) {
            s += ", \"matrix_world\": [";
            for (int r = 0; r < 4; ++r) {
                s += r ? ", [" : "[";
                for (int c = 0; c < 4; ++c) s += (c ? ", " : "") + number(o.matrix[c][r]);
                s += "]";
            }
            s += "]";
        }
        s += "}";
    }
    s += m.objects.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

bool readBridgeManifest(const std::string& path, BridgeManifest& out, std::string& error) {
    std::string text;
    if (!readFile(utf8Path(path), text)) {
        error = "Nothing has been sent yet (no " + path + ")";
        return false;
    }
    return parseBridgeManifest(text, out, error);
}

bool blenderReceive(const std::string& root, std::vector<MeshObject>& out, BridgeManifest* manifest,
                    std::string& message) {
    if (root.empty()) {
        message = "No Blender exchange folder (no user data folder)";
        return false;
    }
    const std::string inbox = blenderInbox(root);
    BridgeManifest m;
    if (!readBridgeManifest(bridgeManifestPath(inbox), m, message)) {
        if (message.rfind("Nothing", 0) == 0) message = "Nothing has been sent from Blender yet";
        return false;
    }
    if (m.objects.empty()) {
        message = "Blender sent no objects";
        return false;
    }
    std::vector<MeshObject> objects;
    std::string warnings;
    for (const BridgeObject& b : m.objects) {
        MeshObject obj;
        obj.name = b.name.empty() ? "Object" : b.name;
        std::string err;
        if (!loadGoz(pathUtf8(utf8Path(inbox) / utf8Path(b.file)), obj.mesh, err)) {
            message = "Cannot load \"" + obj.name + "\" from Blender: " + err;
            return false;
        }
        if (!err.empty()) warnings += "; " + obj.name + ": " + err;
        if (b.hasMatrix) {
            // Into the scene: the add-on sends object-space meshes and each object's matrix_world.
            const glm::dmat4 t = blenderMatrixToSculptForge(b.matrix);
            for (glm::vec3& p : obj.mesh.positions) p = glm::vec3(t * glm::dvec4(glm::dvec3(p), 1.0));
            obj.mesh.recomputeAllNormals();
        }
        objects.push_back(std::move(obj));
    }
    size_t triangles = 0;
    for (const auto& o : objects) triangles += o.mesh.triangleCount();
    message = "Received " + std::to_string(objects.size()) + (objects.size() == 1 ? " object" : " objects") +
              " from Blender (" + std::to_string(triangles) + " triangles)" + warnings;
    out = std::move(objects);
    if (manifest) *manifest = std::move(m);
    return true;
}

bool blenderSend(const std::vector<MeshObjectRef>& objects, const std::string& root, std::string& message,
                 std::vector<std::string>* sentNames, int64_t* revision) {
    if (root.empty()) {
        message = "No Blender exchange folder (no user data folder)";
        return false;
    }
    if (objects.empty()) {
        message = "Nothing visible to send to Blender";
        return false;
    }
    const fs::path outbox = utf8Path(blenderOutbox(root));
    std::error_code ec;
    fs::create_directories(outbox, ec);
    if (ec) {
        message = "Cannot create " + pathUtf8(outbox) + ": " + ec.message();
        return false;
    }
    BridgeManifest m;
    m.revision = std::max<int64_t>(bridgeRevision(pathUtf8(outbox)), 0) + 1;
    m.sender = "sculptforge";
#ifdef SCULPTFORGE_VERSION
    m.senderVersion = SCULPTFORGE_VERSION;
#endif
    const std::string prefix = "r" + std::to_string(m.revision) + "_";
    std::vector<std::string> names;
    for (size_t k = 0; k < objects.size(); ++k) {
        // Blender's names for duplicates: Name, Name.001, Name.002...
        const std::string wanted = objects[k].name.empty() ? "Object" : objects[k].name;
        std::string name = wanted;
        for (int n = 1; std::find(names.begin(), names.end(), name) != names.end(); ++n) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, ".%03d", n);
            name = wanted + suffix;
        }
        names.push_back(name);
        char file[48];
        std::snprintf(file, sizeof file, "%03zu.GoZ", k);
        BridgeObject b;
        b.name = name;
        b.file = prefix + file;
        GozWriteOptions o;
        o.quads = objects[k].mesh->hasQuads(); // only kept quads: triangles of n-gons must stay triangles
        std::string err;
        if (!saveGoz(pathUtf8(outbox / utf8Path(b.file)), *objects[k].mesh, name, err, o)) {
            message = "Cannot send \"" + name + "\" to Blender: " + err;
            return false;
        }
        m.objects.push_back(std::move(b));
    }
    if (!writeAtomically(outbox / "manifest.json", writeBridgeManifest(m))) {
        message = "Cannot write " + pathUtf8(outbox / "manifest.json");
        return false;
    }
    // Earlier sends' files (the add-on has read them, or never will).
    for (const auto& entry : fs::directory_iterator(outbox, ec)) {
        const std::string n = pathUtf8(entry.path().filename());
        if (n.size() > 1 && n[0] == 'r' && n.rfind(prefix, 0) != 0 && n.find('_') != std::string::npos) {
            std::error_code rm;
            fs::remove(entry.path(), rm); // may be open on Windows; the next send tries again
        }
    }
    message = "Sent " + std::to_string(objects.size()) + (objects.size() == 1 ? " object" : " objects") +
              " to Blender: press Receive in Blender's SculptForge panel if it doesn't update by itself";
    if (sentNames) *sentNames = std::move(names);
    if (revision) *revision = m.revision;
    return true;
}

long long bridgeFileTime(const std::string& path) {
    if (path.empty()) return 0;
    std::error_code ec;
    auto t = fs::last_write_time(utf8Path(path), ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

int64_t bridgeRevision(const std::string& folder) {
    BridgeManifest m;
    std::string err;
    return readBridgeManifest(bridgeManifestPath(folder), m, err) ? m.revision : -1;
}

void writeBridgeAppInfo(const std::string& root, const std::string& executable, const std::string& version) {
    if (root.empty() || executable.empty()) return;
    const std::string text = "{\n  \"path\": " + jsonQuote(executable) + ",\n  \"version\": " + jsonQuote(version) + "\n}\n";
    const fs::path p = utf8Path(root) / "app.json";
    std::string old;
    if (readFile(p, old) && old == text) return;
    std::error_code ec;
    fs::create_directories(utf8Path(root), ec);
    writeAtomically(p, text);
}

void writeBridgeHeartbeat(const std::string& root, bool remove) {
    if (root.empty()) return;
    const fs::path p = utf8Path(root) / "alive.json";
    std::error_code ec;
    if (remove) {
        fs::remove(p, ec);
        return;
    }
#ifdef _WIN32
    const long long pid = _getpid();
#else
    const long long pid = getpid();
#endif
    writeAtomically(p, "{\"pid\": " + std::to_string(pid) + ", \"time\": " + number(nowSeconds()) + "}\n");
}

} // namespace sf
