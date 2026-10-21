// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Malformed and hostile input files found by fuzzing the loaders: each must be
// rejected with an error (or loaded safely), never crash or allocate wildly.
#include "TestHarness.h"

#include "core/Mesh.h"
#include "core/Splats.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace sf;

namespace {

std::string writeFile(const char* name, const std::string& bytes) {
    const std::string path = tempPath(name);
    std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
    return path;
}

bool plyLoads(const std::string& text, std::string& err) {
    const std::string path = writeFile("sculptforge_malformed.ply", text);
    Mesh m;
    SplatCloud s;
    PlyContent c;
    const bool ok = loadPly(path, m, s, c, err);
    std::filesystem::remove(path);
    return ok;
}

} // namespace

TEST(ply_header_claiming_billions_of_rows_is_rejected) {
    // A splat header promising 3 billion rows in a file of a few hundred bytes used to allocate before reading.
    std::string err;
    CHECK(!plyLoads("ply\nformat binary_little_endian 1.0\nelement vertex 3000000000\nproperty float x\nproperty float y\n"
                    "property float z\nproperty float scale_0\nproperty float rot_0\nproperty float opacity\nend_header\n"
                    "0123456789abcdef",
                    err));
    CHECK(err.find("more data than the file contains") != std::string::npos);
    CHECK(!plyLoads("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                    "element face 2000000000\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n",
                    err));
}

TEST(ply_negative_or_huge_face_values_are_rejected) {
    std::string err;
    const std::string head = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                             "element face 1\nproperty list int float vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n";
    CHECK(!plyLoads(head + "-3 0 1 2\n", err));     // negative list count
    CHECK(!plyLoads(head + "3 0 -65 2\n", err));    // negative index
    CHECK(!plyLoads(head + "3 0 1 7.8e34\n", err)); // index beyond uint32
    CHECK(plyLoads(head + "3 0 1 2\n", err));       // the well-formed file still loads
}

TEST(non_finite_coordinates_do_not_break_import) {
    // NaN and infinite positions reach the attribute-transfer grid during welding; they must not be cast to int.
    const std::string path = writeFile("sculptforge_malformed.obj",
                                       "v nan 0 0 1 0 0\nv 0 inf 0 0 1 0\nv 0 0 1e38 0 0 1\nv 1 1 1 1 1 1\nf 1 2 3\nf 2 3 4\n");
    Mesh m;
    std::string err;
    loadObj(path, m, err); // either outcome is fine; it must not crash or trip the sanitizers
    std::filesystem::remove(path);
    CHECK(true);
}

TEST(weld_handles_far_off_and_non_finite_positions) {
    Mesh m;
    m.positions = {{1e30f, 0, 0}, {0, 1e30f, 0}, {0, 0, std::nanf("")}, {1, 2, 3}, {1, 2, 3}};
    m.triangles = {{0, 1, 2}, {1, 3, 4}};
    const Mesh w = weldImported(m);
    CHECK(w.positions.size() <= m.positions.size()); // no crash or UB; duplicates may merge
}
