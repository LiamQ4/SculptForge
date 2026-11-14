// Native MetaHuman region masks. Presets match mh_part_sculpt/masks.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CrkwMaskManager.h"
#include "core/Multires.h"
#include "core/Undo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sf {
namespace {
const CrkwMaskPreset presets[] = {
    {"Eye",375,382,62,7,14,3,true}, {"Brow",368,322,80,8,3,3,true},
    {"Nose",500,455,40,40,8,3,false}, {"Lips",500,585,80,6,7,2,false},
    {"Mouth",500,585,80,6,14,4,false}, {"Ear",150,460,22,55,6,2,true},
    {"Cheek",285,545,45,45,9,4,true}, {"Chin",500,700,50,20,6,3,false},
    {"Forehead",500,210,150,30,6,4,false}
};
uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i=0;i<n;++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
std::string topologyKey(const Mesh& m) {
    const size_t n = m.vertexCount();
    uint64_t h = hashBytes(1469598103934665603ull, &n, sizeof n);
    h = hashBytes(h, m.triangles.data(), m.triangles.size()*sizeof(glm::uvec3));
    h = hashBytes(h, m.quadPartners.data(), m.quadPartners.size()*sizeof(uint32_t));
    return std::to_string(h);
}
struct Face { std::array<uint32_t,4> vertices{}; int count=0; glm::vec2 centre{}; };
struct Cache { std::string key; std::vector<Face> faces; std::vector<std::vector<uint32_t>> neighbors; };
thread_local Cache cached;

std::vector<uint8_t> headFlags(const Mesh& mesh, const Json& ref) {
    if (ref["role"].asString() == "head") return std::vector<uint8_t>(mesh.triangleCount(),1);
    std::vector<uint8_t> flags;
    for (const auto& part : ref["parts"].items()) flags.push_back(part.asNumber(-1)==1);
    if (mesh.multires) for (int level=1;level<=subdivCurrentLevel(mesh);++level) {
        const auto& tp = *mesh.multires->levels[size_t(level)].topo;
        std::vector<uint8_t> next(tp.fineTriangles,0);
        for (size_t p=0;p<tp.polygonCount();++p) {
            const auto parent = tp.polyTris[p].x;
            if (parent>=flags.size()) throw std::runtime_error("MetaHuman head partition does not match this topology");
            if (tp.generated) {
                const size_t first=tp.firstFineTriangle(p), last=p+1<tp.polygonCount()?tp.firstFineTriangle(p+1):next.size();
                std::fill(next.begin()+first,next.begin()+last,flags[parent]);
            }
        }
        if (!tp.generated) for (size_t t=0;t<next.size();++t) {
            if (t>=tp.fineParentPoly.size() || tp.fineParentPoly[t]>=tp.polyTris.size())
                throw std::runtime_error("Cannot map MetaHuman head faces at this subdivision level");
            next[t]=flags[tp.polyTris[tp.fineParentPoly[t]].x];
        }
        flags=std::move(next);
    }
    if (flags.size()!=mesh.triangleCount()) throw std::runtime_error("MetaHuman head partition is missing or topology changed");
    return flags;
}
const Cache& cacheFor(const Mesh& mesh, const Json& ref, const std::string& topology) {
    auto flags=headFlags(mesh,ref);
    uint64_t h=hashBytes(1469598103934665603ull,mesh.uvs.data(),mesh.uvs.size()*sizeof(glm::vec2));
    h=hashBytes(h,flags.data(),flags.size());
    const std::string key=topology+":"+std::to_string(h);
    if (cached.key==key) return cached;
    Cache fresh; fresh.key=key; fresh.neighbors.resize(mesh.vertexCount());
    for (uint32_t t=0;t<mesh.triangleCount();++t) {
        const uint32_t partner=mesh.quadPartners.empty()?Mesh::kNoQuad:mesh.quadPartners[t];
        const auto tri=mesh.triangles[t];
        for (int c=0;c<3;++c) {
            const uint32_t a=tri[c],b=tri[(c+1)%3];
            bool diagonal=false;
            if (partner!=Mesh::kNoQuad && partner<mesh.triangleCount()) {
                const auto other=mesh.triangles[partner];
                diagonal=(other.x==a||other.y==a||other.z==a)&&(other.x==b||other.y==b||other.z==b);
            }
            if (!diagonal) { fresh.neighbors[a].push_back(b); fresh.neighbors[b].push_back(a); }
        }
        if (!flags[t] || (partner!=Mesh::kNoQuad && partner<t)) continue;
        Face face;
        for (uint32_t f : {t,partner}) {
            if (f==Mesh::kNoQuad || f>=mesh.triangleCount()) continue;
            for (int c=0;c<3;++c) {
                const uint32_t v=mesh.triangles[f][c];
                if (std::find(face.vertices.begin(),face.vertices.begin()+face.count,v)!=face.vertices.begin()+face.count) continue;
                if (face.count==4) continue;
                face.vertices[face.count++]=v;
                face.centre+=mesh.uvs[size_t(f)*3+c];
            }
        }
        face.centre/=float(face.count);
        face.centre={face.centre.x*1000,(1-face.centre.y)*1000};
        fresh.faces.push_back(face);
    }
    for (auto& ring:fresh.neighbors) { std::sort(ring.begin(),ring.end()); ring.erase(std::unique(ring.begin(),ring.end()),ring.end()); }
    if (fresh.faces.empty()) throw std::runtime_error("No head faces are available");
    cached=std::move(fresh); return cached;
}
std::vector<float> weights(const Mesh& mesh,const Cache& cache,const Json& state,const std::string& name,const std::string& topology) {
    const auto found=std::find_if(std::begin(presets),std::end(presets),[&](const auto& p){return name==p.name;});
    const Json& saved=state["custom"][name];
    if (found==std::end(presets) && !saved.isObject()) throw std::runtime_error("Unknown region: "+name);
    const int rings=int(state["regions"][name]["rings"].asNumber(found!=std::end(presets)?found->rings:0));
    const int feather=int(state["regions"][name]["feather"].asNumber(found!=std::end(presets)?found->feather:0));
    std::vector<int> distances(mesh.vertexCount(),std::numeric_limits<int>::max());
    std::vector<uint32_t> queue;
    auto seed=[&](uint32_t v) { if (v<distances.size() && distances[v]!=0) { distances[v]=0; queue.push_back(v); } };
    if (saved.isObject()) {
        if (saved["topology"].asString()!=topology) throw std::runtime_error("Custom region belongs to another topology/level; return there or save it again");
        for (const auto& v:saved["vertices"].items()) if (v.asNumber(-1)>=0 && v.asNumber()<mesh.vertexCount()) seed(uint32_t(v.asNumber()));
    } else {
        const std::string side=state["side"].asString();
        for (int mirror=0;mirror<(found->paired?2:1);++mirror) {
            if (found->paired && ((side=="R" && mirror) || (side=="L" && !mirror))) continue;
            const float cx=mirror?1000-found->cx:found->cx;
            float best=std::numeric_limits<float>::max(); const Face* nearest=nullptr;
            const size_t before=queue.size();
            for (const auto& face:cache.faces) {
                const float dx=(face.centre.x-cx)/found->rx,dy=(face.centre.y-found->cy)/found->ry;
                const glm::vec2 delta=face.centre-glm::vec2(cx,found->cy);
                const float d=glm::dot(delta,delta);
                if (d<best) {best=d;nearest=&face;}
                if (dx*dx+dy*dy<=1) for (int c=0;c<face.count;++c) seed(face.vertices[c]);
            }
            if (queue.size()==before && nearest) for (int c=0;c<nearest->count;++c) seed(nearest->vertices[c]);
        }
    }
    for (size_t at=0;at<queue.size();++at) {
        const auto v=queue[at]; if (distances[v]>=rings+feather+1) continue;
        for (uint32_t neighbor:cache.neighbors[v]) if (distances[neighbor]==std::numeric_limits<int>::max()) {
            distances[neighbor]=distances[v]+1; queue.push_back(neighbor);
        }
    }
    std::vector<float> out(mesh.vertexCount(),0);
    for (size_t v=0;v<out.size();++v) {
        if (!feather) out[v]=distances[v]<=rings?1.0f:0.0f;
        else {const float t=std::clamp((float(distances[v])-rings)/float(feather+1),0.0f,1.0f);out[v]=1-t*t*(3-2*t);}
    }
    return out;
}
}

std::span<const CrkwMaskPreset> crkwMaskPresets() {return presets;}
const Json& crkwMaskSettings(const Mesh& mesh) {
    if (mesh.crkwMaskState) return *mesh.crkwMaskState;
    static const Json defaults=[] {
    Json state=Json::object(),regions=Json::object();
    for (const auto& p:presets) {Json r=Json::object();r.set("rings",p.rings);r.set("feather",p.feather);regions.set(p.name,std::move(r));}
    state.set("regions",std::move(regions));state.set("custom",Json::object());state.set("active",Json::array());
    state.set("side","BOTH");state.set("selected","Eye");return state;
    }();
    return defaults;
}
void registerCrkwMaskManagerCommands(CommandRegistry& registry) {
    using T=ParamSpec::Type;
    registry.add({"crkw_mh_mask","MetaHuman mask regions: undoable toggles, isolation, ring settings and saved custom regions.",
        {{"action",T::String,"toggle/isolate/add/protect/settings/clear/invert/save/rebuild",false},
         {"region",T::String,"Preset or mhr_ custom region",false},{"side",T::String,"BOTH/R/L",false},
         {"rings",T::Number,"0-60 edge rings",false},{"feather",T::Number,"0-20 feather rings",false},
         {"name",T::String,"Custom region name to save",false}},
        [](Document& doc,const Args& args)->CommandResult {
            try {
            Mesh& mesh=doc.mesh;const auto& ref=doc.activePartInfo().crkwMetaHuman;
            const std::string role=ref["role"].asString();
            if ((role!="head" && role!="full") || !mesh.hasUvs()) return {false,"Select a MetaHuman head or full-body Part with its primary UVs"};
            Json state=crkwMaskSettings(mesh);
            const std::string action=argString(args,"action","toggle"),name=argString(args,"region",state["selected"].asString());
            const std::string side=argString(args,"side",state["side"].asString());
            const double rings=argNumber(args,"rings",state["regions"][name]["rings"].asNumber()),feather=argNumber(args,"feather",state["regions"][name]["feather"].asNumber());
            if ((side!="BOTH"&&side!="R"&&side!="L") || !std::isfinite(rings)||!std::isfinite(feather)||rings<0||rings>60||feather<0||feather>20)
                return {false,"Use side=BOTH/R/L, rings=0-60, feather=0-20"};
            const auto topology=topologyKey(mesh);
            state.set("side",side);state.set("selected",name);
            Json settings=Json::object();settings.set("rings",int(rings));settings.set("feather",int(feather));
            Json regions=state["regions"];regions.set(name,std::move(settings));state.set("regions",std::move(regions));
            std::vector<std::string> active;
            for (const auto& value:state["active"].items()) active.push_back(value.asString());
            std::vector<float> mask(mesh.vertexCount(),0);
            if (action=="save") {
                const std::string customName=argString(args,"name");
                if (customName.empty()||customName.size()>64) return {false,"Give the custom region a name (1-64 bytes)"};
                const std::string key="mhr_"+customName;
                Json saved=Json::object(),vertices=Json::array();
                for (uint32_t v=0;v<mesh.vertexCount();++v) if (mesh.maskAt(v)<.5f) vertices.push(double(v));
                if (!vertices.size()) return {false,"No unmasked vertices to save"};
                saved.set("vertices",std::move(vertices));saved.set("topology",topology);
                Json custom=state["custom"];custom.set(key,std::move(saved));state.set("custom",std::move(custom));
                state.set("selected",key);
                Json customSettings=Json::object();customSettings.set("rings",0);customSettings.set("feather",0);
                regions=state["regions"];regions.set(key,std::move(customSettings));state.set("regions",std::move(regions));
                for (size_t v=0;v<mask.size();++v) mask[v]=mesh.maskAt(uint32_t(v));
            } else if (action=="clear"||action=="invert") {
                active.clear();
                for (size_t v=0;v<mask.size();++v) mask[v]=action=="clear"?0:1-mesh.maskAt(uint32_t(v));
            } else {
                const auto& cache=cacheFor(mesh,ref,topology);
                if (action=="toggle"||action=="isolate") {
                    if (action=="isolate") active.clear();
                    const auto found=std::find(active.begin(),active.end(),name);
                    if (found!=active.end()&&action=="toggle") active.erase(found);
                    else if (found==active.end()) active.push_back(name);
                } else if (action!="settings"&&action!="rebuild"&&action!="protect"&&action!="add") return {false,"Unknown mask-manager action"};
                if (action=="protect"||action=="add") {
                    const auto w=weights(mesh,cache,state,name,topology);active.clear();
                    for (size_t v=0;v<mask.size();++v) mask[v]=action=="protect"?std::max(mesh.maskAt(uint32_t(v)),w[v]):std::min(mesh.maskAt(uint32_t(v)),1-w[v]);
                } else if (!active.empty()) {
                    std::fill(mask.begin(),mask.end(),1);
                    for (const auto& region:active) {
                        const auto w=weights(mesh,cache,state,region,topology);
                        for (size_t v=0;v<mask.size();++v) mask[v]=std::min(mask[v],1-w[v]);
                    }
                }
            }
            Json enabled=Json::array();for (const auto& value:active) enabled.push(value);state.set("active",std::move(enabled));
            UndoRecord record=UndoRecord::captureMask(mesh,"MetaHuman Mask Manager");record.crkwMaskState=mesh.crkwMaskState;
            mesh.mask=std::move(mask);mesh.crkwMaskState=std::make_shared<Json>(std::move(state));mesh.markAllDirty();
            doc.undo.push(std::move(record));doc.modified=true;
            return {true,"MetaHuman mask: "+action+" "+(action=="save"?argString(args,"name"):name)};
            } catch (const std::exception& error) { return {false,error.what()}; }
        }});
}
} // namespace sf
