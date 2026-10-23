// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// qremesh and project_all: the scriptable side of Geometry > QRemesh and SubTool > Project (core/QRemesh.h,
// core/Projection.h). The app runs QRemesh on a worker thread and applies the result with applyQRemesh.
#include "core/Commands.h"
#include "core/Projection.h"
#include "core/QRemesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

using T = ParamSpec::Type;

int symmetryAxisArg(const std::string& s) {
    if (s == "x" || s == "X") return 0;
    if (s == "y" || s == "Y") return 1;
    if (s == "z" || s == "Z") return 2;
    return -1;
}

} // namespace

std::string applyQRemesh(Document& doc, Mesh result, bool keepOriginal, const QRemeshStats& st) {
    const size_t before = doc.mesh.triangleCount();
    if (keepOriginal) {
        const SubTool& active = doc.activeSubtoolInfo();
        const int at = doc.activeSubtool + 1;
        doc.addSubtool(doc.mesh, active.name + " Original", false, at);
        doc.subtools[size_t(at)].visible = false;
    }
    doc.replaceMeshUndoable(std::move(result), "QRemesh");
    const size_t faces = st.quads + st.triangles;
    char buf[256];
    std::snprintf(buf, sizeof buf, "QRemesh: %zu quads%s from %zu triangles (%.0f%% quads%s) in %.1f s%s", st.quads,
                  st.triangles ? (" + " + std::to_string(st.triangles) + " triangles").c_str() : "", before,
                  faces ? 100.0 * double(st.quads) / double(faces) : 0.0,
                  st.nonManifoldEdges ? (", " + std::to_string(st.nonManifoldEdges) + " non-manifold edges").c_str() : "",
                  st.seconds, keepOriginal ? "; the original is kept as a hidden SubTool" : "");
    return buf;
}

void registerRetopoCommands(CommandRegistry& reg) {
    reg.add({"qremesh",
             "QRemesh (like ZBrush's ZRemesher): automatic quad retopology of the active SubTool. Builds clean, "
             "all-quad topology with edge loops that follow the form, close to the target polygon count. Sculpt "
             "layers are baked; mask and polypaint carry over. Undoable. Use project_all afterwards (from a kept "
             "original) to bring fine detail back onto a divided copy.",
             {{"target", T::Number, "Target polygon (quad) count, 16-2000000. Default 5000.", false},
              {"adaptive", T::Number, "Adaptive Size 0-1: 0 = even quads, 1 = small quads in curved areas, big "
                                      "ones on flat areas. Default 0.5.", false},
              {"keep_groups", T::Bool, "Keep Groups: edge loops follow the polygroup borders and the groups are kept.", false},
              {"use_polypaint", T::Bool, "Use the polypaint as a density guide: red areas get up to 4x the quads, blue "
                                         "areas down to a quarter.", false},
              {"symmetry", T::String, "Mirror the topology across x, y or z (the plane through the origin). Default: none.", false},
              {"keep_original", T::Bool, "Keep the original as a hidden SubTool below (for project_all). Default false.", false},
              {"smooth", T::Number, "Relaxation passes on the surface (0-30). Default 6.", false},
              {"seed", T::Number, "Random start of the direction field (results are repeatable per seed).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "QRemesh: the SubTool is empty"};
                 QRemeshOptions o;
                 o.targetQuads = int(std::clamp(argNumber(a, "target", 5000), 16.0, 2'000'000.0));
                 o.adaptive = float(std::clamp(argNumber(a, "adaptive", 0.5), 0.0, 1.0));
                 o.keepGroups = argBool(a, "keep_groups", false);
                 o.usePolypaint = argBool(a, "use_polypaint", false);
                 o.symmetryAxis = symmetryAxisArg(argString(a, "symmetry"));
                 o.smoothIterations = int(std::clamp(argNumber(a, "smooth", 6), 0.0, 30.0));
                 o.seed = uint32_t(std::max(0.0, argNumber(a, "seed", 1)));
                 Mesh out;
                 std::string err;
                 QRemeshStats st;
                 if (!qremesh(doc.mesh, o, out, err, {}, &st)) return {false, err};
                 return {true, applyQRemesh(doc, std::move(out), argBool(a, "keep_original", false), st)};
             }});

    reg.add({"project_all",
             "ProjectAll (SubTool palette): move the active SubTool's vertices onto the surface of the other visible "
             "SubTools and take their polypaint, e.g. after QRemesh and a few subdivisions, to get the sculpted detail "
             "back. Respects the mask (masked vertices stay). One undo step.",
             {{"distance", T::Number, "Dist: how far each vertex searches along its normal, as a fraction of the "
                                      "SubTool's bounding-box diagonal (0.001-1). Default 0.02.", false},
              {"mean", T::Bool, "Mean: with surface on both sides, settle halfway between them. Default false.", false},
              {"blur", T::Number, "PA Blur: smoothing passes over the projected offsets (0-20). Default 0.", false},
              {"geometry", T::Bool, "Project the shape. Default true.", false},
              {"polypaint", T::Bool, "Project the polypaint (when a source has any). Default true.", false},
              {"source", T::String, "Project only from the SubTool with this name (default: all visible ones).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 ProjectOptions o;
                 o.distance = float(std::clamp(argNumber(a, "distance", 0.02), 0.001, 1.0));
                 o.mean = argBool(a, "mean", false);
                 o.blur = int(std::clamp(argNumber(a, "blur", 0), 0.0, 20.0));
                 o.geometry = argBool(a, "geometry", true);
                 o.polypaint = argBool(a, "polypaint", true);
                 int source = -1;
                 if (a.contains("source")) {
                     source = doc.findSubtool(argString(a, "source"));
                     if (source < 0) return {false, "No SubTool called '" + argString(a, "source") + "'"};
                     if (source == doc.activeSubtool) return {false, "ProjectAll can't project a SubTool onto itself"};
                 }
                 std::string msg;
                 const bool ok = projectAll(doc, o, msg, source);
                 return {ok, msg};
             }});
}

} // namespace sf
