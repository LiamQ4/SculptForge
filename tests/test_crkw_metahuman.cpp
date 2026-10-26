// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "io/FbxIO.h"
#include <chrono>
#include <fstream>

namespace {
struct Fixture {
    std::filesystem::path root;
    Fixture() {
        root = std::filesystem::temp_directory_path() / ("crkw-mh-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        sf::Mesh mesh = sf::makeCube(1, .2f);
        mesh.quadPartners.clear();
        mesh.uvs.resize(mesh.triangleCount() * 3);
        for (size_t t = 0; t < mesh.triangleCount(); ++t) {
            mesh.uvs[t * 3] = {0, 0}; mesh.uvs[t * 3 + 1] = {1, 0}; mesh.uvs[t * 3 + 2] = {1, 1};
        }
        // A seam with distinct control points at the same position must not be welded.
        mesh.positions.push_back(mesh.positions[0]);
        for (auto& tri : mesh.triangles) if (tri.x == 0) { tri.x = uint32_t(mesh.positions.size() - 1); break; }
        mesh.rebuildTopology(); mesh.recomputeAllNormals();
        sf::FbxExportOptions options; options.scale = 100;
        std::string error;
        CHECK(sf::saveFbx((root / "head.fbx").string(), std::vector<sf::FbxObject>{{"head_lod0_mesh", {&mesh}},
            {"eyeLeft_lod0_mesh", {&mesh}}, {"eyeRight_lod0_mesh", {&mesh}}, {"teeth_lod0_mesh", {&mesh}}}, options, error));
        CHECK(sf::saveFbx((root / "body.fbx").string(), "body_lod0_mesh", {&mesh}, options, error));
        sf::Image image; image.width = image.height = 4; image.rgba.assign(64, 180);
        for (size_t i = 3; i < image.rgba.size(); i += 4) image.rgba[i] = 255;
        for (const auto* name : {"T_Gray_Head_D", "T_Gray_Head_D_Guides", "T_Gray_Head_D_Guides_Annotated",
                                "T_Gray_Body_D", "T_Gray_Body_D_Guides", "T_Gray_Eyes_D", "T_Gray_Teeth_D"})
            CHECK(image.savePng((root / (std::string(name) + ".PNG")).string(), error));
    }
    ~Fixture() {
        const auto expectedParent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
        if (std::filesystem::weakly_canonical(root.parent_path()) == expectedParent && root.filename().string().rfind("crkw-mh-test-", 0) == 0) {
            std::error_code ec; std::filesystem::remove_all(root, ec);
        }
    }
};
}

TEST(crkw_metahuman_reference_preview_export_and_project) {
    Fixture fixture;
    sf::Document doc;
    sf::CommandRegistry commands;
    CHECK(commands.execute(doc, "crkw_mh_create", {{"assets", fixture.root.string()}, {"layout", std::string("HEAD")}}).ok);
    CHECK(doc.mesh.vertexCount() == 9);
    CHECK(doc.activeSubtoolInfo().crkwMetaHuman["role"].asString() == "head");
    const auto originalUvs = doc.mesh.uvs;
    const auto originalPositions = doc.mesh.positions;
    doc.mesh.texture = sf::makeTexture(4, 4);
    const auto originalTexture = doc.mesh.texture;
    CHECK(commands.execute(doc, "crkw_mh_preview", {{"mode", std::string("ANNOTATED")}}).ok);
    CHECK(doc.mesh.previewTexture != nullptr);
    CHECK(doc.mesh.texture == originalTexture);
    CHECK(doc.mesh.uvs == originalUvs);
    const auto project = (fixture.root / "character.sfproj").string();
    std::string error;
    CHECK(sf::saveProject(project, doc, error));
    sf::Document loaded;
    CHECK(sf::loadProject(project, loaded, error));
    CHECK(loaded.mesh.previewTexture != nullptr);
    CHECK(loaded.mesh.texture != nullptr);
    CHECK(loaded.mesh.texture->pixels == originalTexture->pixels);
    CHECK(loaded.activeSubtoolInfo().crkwMetaHuman["role"].asString() == "head");
    loaded.mesh.uvs[0] = {9, 9};
    CHECK(commands.execute(loaded, "crkw_mh_fix_uvs").ok);
    CHECK(loaded.mesh.uvs == originalUvs);
    CHECK(loaded.mesh.positions == originalPositions);
    CHECK(loaded.mesh.previewTexture == nullptr);
    const auto folder = fixture.root / "export";
    std::filesystem::create_directory(folder);
    { std::ofstream marker(folder / "keep.txt"); marker << "preserve"; }
    auto exported = commands.execute(loaded, "crkw_mh_export", {{"folder", folder.string()}});
    if (!exported.ok) std::printf("Export error: %s\n", exported.message.c_str());
    CHECK(exported.ok);
    for (const char* role : {"head", "eyeLeft", "eyeRight", "teeth"}) {
        std::vector<sf::MeshObject> objects;
        CHECK(sf::loadFbxTemplate((folder / (std::string(role) + ".fbx")).string(), objects, error));
        CHECK(objects.size() == 1);
        CHECK(objects[0].mesh.vertexCount() == 9);
        CHECK(objects[0].mesh.uvs == originalUvs);
    }
    CHECK(commands.execute(loaded, "crkw_mh_export", {{"folder", folder.string()}}).ok);
    CHECK(std::filesystem::exists(folder / "keep.txt"));
    loaded.mesh.triangles[0].x = (loaded.mesh.triangles[0].x + 1) % uint32_t(loaded.mesh.vertexCount());
    CHECK(!commands.execute(loaded, "crkw_mh_fix_uvs").ok);
    CHECK(!commands.execute(loaded, "crkw_mh_export", {{"folder", folder.string()}}).ok);
}

TEST(crkw_metahuman_separate_base_export_and_missing_assets) {
    Fixture fixture;
    sf::Document doc;
    sf::CommandRegistry commands;
    CHECK(!commands.execute(doc, "crkw_mh_create", {{"assets", (fixture.root / "missing").string()}}).ok);
    CHECK(doc.subtoolCount() == 1);
    CHECK(commands.execute(doc, "crkw_mh_create", {{"assets", fixture.root.string()}, {"layout", std::string("SEPARATE")}}).ok);
    CHECK(doc.subtoolCount() == 6);
    CHECK(commands.execute(doc, "subdiv_divide").ok);
    CHECK(commands.execute(doc, "crkw_mh_export", {{"folder", (fixture.root / "base-export").string()}}).ok);
    std::vector<sf::MeshObject> objects; std::string error;
    CHECK(sf::loadFbxTemplate((fixture.root / "base-export/head.fbx").string(), objects, error));
    CHECK(objects[0].mesh.vertexCount() == 9);
    CHECK(!commands.execute(doc, "crkw_mh_fix_uvs").ok); // require base level for in-place UV repair
}
