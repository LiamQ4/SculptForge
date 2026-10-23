// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multiresolution subdivision levels, ZBrush style (Tool > Geometry: Divide, Lower Res / Higher Res, SDiv,
// Del Lower / Del Higher, Reconstruct Subdiv, Smt).
//
// A SubTool's mesh may carry a stack of levels (Mesh::multires). Level 0 is the base cage; each higher level
// is the one below subdivided (Catmull-Clark when the mesh has quads, so quads stay quads; Loop on pure
// triangle meshes; or flat when Smt was off) plus a detail displacement per vertex, stored in the local frame
// (tangent, bitangent, normal) of the subdivided surface:
//
//     P[k] = S(P[k-1]) + Frame(S(P[k-1])) * D[k]
//
// so reshaping a low level carries the high-level detail along. The Mesh itself always holds the current
// level, so every brush, command, exporter and the renderer work on it unchanged. The stack keeps a
// reference copy of every level; edits made to the current level are reconciled lazily (subdivCommit), when
// the level changes, the project is saved or a level is added or deleted:
//  - shape: the change is restricted down to every lower level (each coarse vertex moves by a blend of the
//    change at its own and its neighbouring fine vertices), the details of the levels in between are
//    re-fitted so the current level stays exactly as sculpted, and higher levels are re-evaluated from their
//    unchanged details on the way up;
//  - mask, polypaint and paint layers (per vertex), polygroups and hidden faces (per triangle) and UVs (per
//    corner) are copied down to the parent elements and interpolated / inherited up, only where they changed,
//    so detail painted at a high level survives an edit somewhere else at a low level;
//  - sculpt layers are their own displacement channels (world space), subdivided with the same stencils;
//  - the painted texture lives in UV space and is shared by every level.
// Stepping between levels that weren't edited copies the stored level back, so up / down round trips are
// bit-for-bit.
//
// The stack is shared copy-on-write (std::shared_ptr): copying a Mesh for an undo snapshot or an export is
// cheap, and changing one level only copies that level. Stepping levels is one undo step (UndoRecord::
// subdiv), and so are Divide, Del Lower, Del Higher and Reconstruct: each keeps the previous stack, which
// shares every level the operation didn't touch.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class BVH;
struct Document;

// How level k is built from level k-1. Vertex, edge and face points of the coarse level map to fine vertices
// through vertexChild / edgeChild / faceChild, so a reconstructed level keeps the vertex order it was found in.
struct SubdivTopology {
    bool catmullClark = false; // quads (and triangles) -> quads; false = Loop, triangles -> 4 triangles
    bool smooth = true;        // false: flat subdivision (Smt off), the shape is not smoothed
    uint32_t coarseVertices = 0, fineVertices = 0;
    // The quad pairing of the coarse level's triangles this level was built with (Mesh::kNoQuad = triangle).
    std::vector<uint32_t> pairing;
    // Coarse polygons (triangles and quads), corners in winding order (CSR).
    std::vector<uint32_t> polyOffsets, polyVerts;
    std::vector<uint32_t> polyEdges;  // per polygon side i (corner i -> i + 1): its edge
    std::vector<uint32_t> polyCorner; // per polygon corner: the coarse triangle corner (tri * 3 + slot) for UVs
    std::vector<glm::uvec2> polyTris; // per polygon: its coarse triangles (y = kNone for a triangle)
    // Coarse edges (a < b) and the polygons on each side; y = kNone on boundaries and non-manifold edges.
    std::vector<glm::uvec2> edges, edgeFaces;
    std::vector<uint32_t> vertEdgeOffsets, vertEdges; // coarse vertex -> incident edges (CSR)
    std::vector<uint32_t> vertexChild, edgeChild, faceChild; // fine vertex of each coarse vertex / edge / polygon
    // Per fine triangle: the polygon and the coarse triangle it lies in, and per fine corner where it sits on
    // that polygon (0-3 = corner i, 4-7 = midpoint of side i, 8 = centre), for UVs.
    std::vector<uint32_t> fineParentPoly, fineParentTri;
    std::vector<uint8_t> fineCode;
    std::vector<uint32_t> frameRef; // per fine vertex: the neighbour that fixes its tangent direction
    static constexpr uint32_t kNone = ~0u;

    size_t polygonCount() const { return polyOffsets.empty() ? 0 : polyOffsets.size() - 1; }
    size_t bytes() const;
};

// One level of the stack. `data` is the level as a mesh without topology (positions, triangles, every
// attribute, sculpt and paint layers; no texture). For levels above the one last edited `stale` is set and
// data->positions / base / layer deltas are out of date until the level is evaluated again.
struct SubdivLevel {
    std::shared_ptr<Mesh> data;
    std::shared_ptr<const SubdivTopology> topo; // null for level 0
    std::shared_ptr<std::vector<glm::vec3>> detail; // local-frame displacement (levels >= 1)
    std::vector<std::shared_ptr<std::vector<glm::vec3>>> layerDetail; // per sculpt layer, world space
    bool stale = false;
};

struct MultiresStack {
    std::vector<SubdivLevel> levels; // at least 2 while a mesh has a stack
    int current = 0;
};

// --- Queries (0 levels = a plain mesh) ---
int subdivLevelCount(const Mesh& mesh);
int subdivCurrentLevel(const Mesh& mesh); // 0-based; 0 without levels
// "SDiv 3/5" (1-based, as ZBrush shows it), or "" without levels.
std::string subdivLevelText(const Mesh& mesh);
// Triangle count of level `level`.
size_t subdivLevelTriangles(const Mesh& mesh, int level);
// True when the stack still describes this mesh (vertex and triangle counts of the current level match);
// Mesh::sanitizeAttributes drops a stack that doesn't, so topology edits can't leave a stale one behind.
bool subdivMatches(const Mesh& mesh);
// Bytes the stack holds (all levels, details and topology), for the memory readout and tests.
size_t subdivBytes(const Mesh& mesh);

// --- Operations on the mesh (no undo; see the commands / Document helpers below) ---
// Each returns false with `error` set when it can't run. All of them reconcile pending edits first.
//
// Adds a level on top (stepping to the highest level first) and makes it current. `smooth` = Smt.
bool subdivDivide(Mesh& mesh, bool smooth, std::string& error, size_t maxTriangles = 50'000'000);
// Makes `level` current. Rebuilds topology and normals (the caller rebuilds the BVH).
bool subdivSetLevel(Mesh& mesh, int level, std::string& error);
// Deletes the levels below / above the current one (the current level becomes the base / the top).
bool subdivDeleteLower(Mesh& mesh, std::string& error);
bool subdivDeleteHigher(Mesh& mesh, std::string& error);
// At the lowest level: finds the mesh this one was subdivided from (a regular Catmull-Clark subdivision of
// an all-quad mesh, or a Loop subdivision of a triangle mesh), adds it as a new lowest level and steps down
// to it. The current level keeps its exact shape and vertex order.
bool subdivReconstruct(Mesh& mesh, std::string& error);
// Folds edits of the current level into the stack (see the file comment). Cheap when nothing changed.
// Returns false (and drops the stack) if the mesh no longer has the current level's topology.
bool subdivCommit(Mesh& mesh);
// A full copy of level `level` (pending edits included), e.g. to export the base cage.
Mesh subdivLevelMesh(const Mesh& mesh, int level);

// --- Project files ---
// Rebuilds the topology of a saved level (`fine`, built from `coarse`) from what the file keeps: the scheme,
// Smt, the quad pairing and the child maps. Null with `error` set when they don't fit the meshes.
std::shared_ptr<const SubdivTopology> rebuildSubdivTopology(const Mesh& coarse, const Mesh& fine,
                                                            std::vector<uint32_t> pairing, bool catmullClark,
                                                            bool smooth, std::vector<uint32_t> vertexChild,
                                                            std::vector<uint32_t> edgeChild,
                                                            std::vector<uint32_t> faceChild, std::string& error);
// Gives a loaded mesh (the current level) its stack; false when the stack doesn't fit it.
bool attachSubdivStack(Mesh& mesh, MultiresStack stack, std::string& error);
// The level data `level` keeps (for writing projects; may be stale, see SubdivLevel).
const Mesh& subdivLevelData(const Mesh& mesh, int level);
// True when the mesh holds no edits its stack hasn't folded in yet (its current level's data is the mesh).
bool subdivInSync(const Mesh& mesh);

// --- Undoable document operations (one undo step each). Return a message for the status bar / console. ---
bool divideUndoable(Document& doc, bool smooth, std::string& message);
bool setSubdivLevelUndoable(Document& doc, int level, std::string& message);
bool deleteLowerUndoable(Document& doc, std::string& message);
bool deleteHigherUndoable(Document& doc, std::string& message);
bool reconstructUndoable(Document& doc, std::string& message);

// The undo step of a level operation (UndoRecord::subdiv): the level stack before it (after folding in
// pending edits), so applying swaps stacks and loads the other stack's current level. `plain` is the mesh
// without levels, when one side of the step has none. Cheap: the stacks share every level not changed.
struct SubdivUndo {
    std::shared_ptr<MultiresStack> stack;
    std::shared_ptr<Mesh> plain;
    size_t bytes() const;
};
// Swaps `r` with the mesh's stack (so it then holds the inverse) and rebuilds the BVH.
void applySubdivUndo(SubdivUndo& r, Mesh& mesh, BVH& bvh);

// subdiv_divide, subdiv_level, subdiv_lower, subdiv_higher, subdiv_delete_lower, subdiv_delete_higher,
// subdiv_reconstruct, subdiv_info.
void registerSubdivCommands(CommandRegistry& registry);

} // namespace sf
