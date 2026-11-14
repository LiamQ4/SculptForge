// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/CrkwMaskManager.h"
#include "core/Multires.h"
#include "core/Project.h"
#include <algorithm>
#include <cmath>
using namespace sf;
namespace {
Mesh headGrid() {
    Mesh mesh;
    for (int side=0;side<2;++side) {
        const uint32_t start=uint32_t(mesh.positions.size());
        for (int y=0;y<6;++y) for (int x=0;x<6;++x) mesh.positions.emplace_back(float(x)+side*10,float(y),0);
        for (int y=0;y<5;++y) for (int x=0;x<5;++x) {
            const uint32_t a=start+y*6+x,b=a+1,c=a+7,d=a+6;
            const uint32_t t=uint32_t(mesh.triangles.size());
            mesh.triangles.emplace_back(a,b,c);mesh.triangles.emplace_back(a,c,d);
            mesh.quadPartners.push_back(t+1);mesh.quadPartners.push_back(t);
            auto uv=[&](int xx,int yy){return glm::vec2(((side?625:375)+(xx-2.5f)*40)/1000,(1000-(382+(yy-2.5f)*20))/1000);};
            for (auto p:{uv(x,y),uv(x+1,y),uv(x+1,y+1),uv(x,y),uv(x+1,y+1),uv(x,y+1)}) mesh.uvs.push_back(p);
        }
    }
    mesh.rebuildTopology();mesh.recomputeAllNormals();return mesh;
}
void identify(Document& doc,const char* role="head") {doc.activePartInfo().crkwMetaHuman=Json::object();doc.activePartInfo().crkwMetaHuman.set("role",role);}
size_t freeCount(const Mesh& mesh) {size_t n=0;for(uint32_t v=0;v<mesh.vertexCount();++v)n+=mesh.maskAt(v)<.5f;return n;}
}

TEST(crkw_mask_manager_sides_union_undo_and_geometry_safety) {
    CommandRegistry commands;Document doc=makeDoc(headGrid());identify(doc);
    const auto positions=doc.mesh.positions;const auto triangles=doc.mesh.triangles;const auto uv=doc.mesh.uvs;
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("isolate")},{"region",std::string("Eye")},{"rings",0.0},{"feather",0.0},{"side",std::string("R")}}).ok);
    const auto right=doc.mesh.mask;
    CHECK(freeCount(doc.mesh)==8);
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("settings")},{"side",std::string("L")}}).ok);
    const auto left=doc.mesh.mask;CHECK(freeCount(doc.mesh)==8);
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("settings")},{"side",std::string("BOTH")}}).ok);
    CHECK(freeCount(doc.mesh)==16);
    for(size_t v=0;v<right.size();++v) CHECK(doc.mesh.mask[v]==std::min(right[v],left[v]));
    const auto both=doc.mesh.mask;
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"region",std::string("Eye")}}).ok);
    CHECK(freeCount(doc.mesh)==doc.mesh.vertexCount());
    CHECK(crkwMaskSettings(doc.mesh)["active"].size()==0);
    CHECK(commands.execute(doc,"undo").ok);CHECK(doc.mesh.mask==both);
    CHECK(crkwMaskSettings(doc.mesh)["active"].size()==1);
    CHECK(commands.execute(doc,"redo").ok);CHECK(crkwMaskSettings(doc.mesh)["active"].size()==0);
    CHECK(doc.mesh.positions==positions);CHECK(doc.mesh.triangles==triangles);CHECK(doc.mesh.uvs==uv);
}

TEST(crkw_mask_manager_custom_quad_rings_feather_and_project_roundtrip) {
    CommandRegistry commands;Document doc=makeDoc(headGrid());identify(doc);
    doc.mesh.mask.assign(doc.mesh.vertexCount(),1);doc.mesh.mask[14]=0;
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("save")},{"name",std::string("Dot")}}).ok);
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("isolate")},{"region",std::string("mhr_Dot")},{"rings",1.0},{"feather",0.0}}).ok);
    CHECK(freeCount(doc.mesh)==5); // quad diagonals are not edge rings
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("settings")},{"rings",0.0},{"feather",1.0}}).ok);
    CHECK(doc.mesh.mask[14]==0);CHECK(std::abs(doc.mesh.mask[15]-.5f)<1e-6f);CHECK(doc.mesh.mask[21]==1);
    std::string error;const auto file=tempPath("crkw-mask-manager-test.sfproj");
    CHECK(saveProject(file,doc,error));Document restored;CHECK(loadProject(file,restored,error));
    CHECK(crkwMaskSettings(restored.mesh)["custom"]["mhr_Dot"].isObject());
    CHECK(restored.mesh.mask==doc.mesh.mask);
    CHECK(commands.execute(restored,"crkw_mh_mask",{{"action",std::string("clear")}}).ok);
    CHECK(commands.execute(restored,"crkw_mh_mask",{{"action",std::string("isolate")},{"region",std::string("mhr_Dot")}}).ok);
    CHECK(freeCount(restored.mesh)==1);
    CHECK(subdivDivide(restored.mesh,true,error));
    CHECK(restored.mesh.crkwMaskState!=nullptr);
    CHECK(!commands.execute(restored,"crkw_mh_mask",{{"action",std::string("rebuild")}}).ok);
    std::filesystem::remove(file);
}

TEST(crkw_mask_manager_full_body_excludes_body_and_operations) {
    CommandRegistry commands;Document doc=makeDoc(headGrid());identify(doc,"full");
    Json parts=Json::array();for(size_t t=0;t<doc.mesh.triangleCount();++t)parts.push(t<50?0:1);
    doc.activePartInfo().crkwMetaHuman.set("parts",parts);
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("isolate")},{"region",std::string("Eye")},{"rings",0.0},{"feather",0.0}}).ok);
    for(size_t v=0;v<36;++v) CHECK(doc.mesh.mask[v]==1);
    const auto previous=doc.mesh.mask;
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("invert")}}).ok);
    for(size_t v=0;v<previous.size();++v) CHECK(doc.mesh.mask[v]==1-previous[v]);
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("clear")}}).ok);
    CHECK(freeCount(doc.mesh)==doc.mesh.vertexCount());
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("add")},{"region",std::string("Eye")}}).ok);
    CHECK(freeCount(doc.mesh)==doc.mesh.vertexCount());
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("protect")},{"region",std::string("Eye")}}).ok);
    CHECK(freeCount(doc.mesh)<doc.mesh.vertexCount());
    const auto mask=doc.mesh.mask;
    CHECK(!commands.execute(doc,"crkw_mh_mask",{{"side",std::string("invalid")}}).ok);
    CHECK(doc.mesh.mask==mask);
    std::string error;CHECK(subdivDivide(doc.mesh,true,error));
    CHECK(commands.execute(doc,"crkw_mh_mask",{{"action",std::string("isolate")},{"region",std::string("Eye")}}).ok);
    for(uint32_t v=0;v<doc.mesh.vertexCount();++v) if(doc.mesh.positions[v].x<6) CHECK(doc.mesh.maskAt(v)==1);
}

// Mask names belong to the artist; upstream's terminology migration must not rename their keys.
TEST(crkw_mask_names_and_linked_template_identity_survive_project_roundtrip) {
    CommandRegistry commands; Document doc = makeDoc(headGrid()); identify(doc);
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 1); doc.mesh.mask[14] = 0;
    const std::string userName = "\x4d\x61\x74\x63\x61\x70"; // an older material term, as artist-owned text
    const std::string region = "mhr_" + userName;
    CHECK(commands.execute(doc, "crkw_mh_mask", {{"action", std::string("save")}, {"name", userName}}).ok);
    const std::string identity = doc.activePartInfo().crkwMetaHuman.dump();
    const int linked = doc.linkedDuplicate(doc.activePart, glm::vec3(2, 0, 0));
    CHECK(doc.parts[size_t(linked)].crkwMetaHuman.dump() == identity);
    std::string error; const auto file = tempPath("crkw-linked-mask-names.sfproj");
    CHECK(saveProject(file, doc, error)); Document restored; CHECK(loadProject(file, restored, error));
    CHECK(restored.partCount() == 2);
    CHECK(restored.parts[0].crkwMetaHuman.dump() == identity);
    CHECK(restored.parts[1].crkwMetaHuman.dump() == identity);
    CHECK(restored.linkCount(0) == 2);
    CHECK(crkwMaskSettings(restored.mesh)["custom"][region].isObject());
    CHECK(commands.execute(restored, "crkw_mh_mask", {{"action", std::string("isolate")}, {"region", region}}).ok);
    CHECK(freeCount(restored.mesh) == 1);
    std::filesystem::remove(file);
}
