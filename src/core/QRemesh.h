// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh (Geometry > QRemesh, like ZBrush's ZRemesher): automatic quad retopology. A field-aligned method
// after Instant Field-Aligned Meshes (Jakob, Tarini, Panozzo, Sorkine-Hornung, SIGGRAPH Asia 2015; the
// reference code is BSD-3, this is a fresh implementation of the parts we need):
//
//  1. A multi-resolution hierarchy of the surface's vertex graph (greedy edge matching, ~halving per level).
//  2. A smooth 4-RoSy orientation field (where the quad edges point), solved coarse to fine.
//  3. A position field: per vertex the nearest corner of a local quad lattice with the wanted edge length,
//     made consistent across neighbours (also coarse to fine). Adaptive Size scales the lattice with the
//     curvature; polypaint can ask for more (red) or fewer (blue) polygons.
//  4. Extraction: vertices whose lattice corners coincide merge, lattice-neighbours become edges, the graph's
//     faces are traced in each vertex's tangent plane, and every face (quad, or the odd triangle / pentagon
//     around a field singularity) is split into quads, so the result is all quads and closed (2-manifold)
//     where the sculpt is.
//  5. The quads are relaxed and snapped back onto the sculpt; the target count is met by adjusting the
//     lattice size and extracting again.
//
// Keep Groups constrains the fields to the polygroup borders, so edge loops run along them (and the result
// keeps the groups). Symmetry remeshes one half (cut at the plane) and mirrors it, welded on the seam.
#pragma once

#include "core/Mesh.h"

#include <functional>
#include <string>

namespace sf {

class CommandRegistry;
struct Document;

struct QRemeshOptions {
    int targetQuads = 5000;  // ZBrush's Target Polygons Count (here in polygons, not thousands)
    float adaptive = 0.5f;   // Adaptive Size: 0 = even quads, 1 = sizes follow the curvature strongly
    bool keepGroups = false; // edge loops along polygroup borders; the result keeps the groups
    bool detectEdges = true; // hard edges (faces meeting at more than 50 degrees) get an edge loop
    bool usePolypaint = false; // red polypaint: up to 4x the density, blue: down to 1/4
    int symmetryAxis = -1;   // -1 none, 0 X, 1 Y, 2 Z (mirror across the plane through the origin)
    int smoothIterations = 6;
    uint32_t seed = 1;       // the field's random start (the result is deterministic for a seed)
};

struct QRemeshStats {
    size_t quads = 0, triangles = 0; // faces of the result (triangles only for degenerate leftovers)
    size_t boundaryEdges = 0, nonManifoldEdges = 0;
    int attempts = 0;      // extractions run to approach the target count
    float edgeLength = 0;  // mean quad edge length
    double seconds = 0;
};

// Fraction 0..1 of the whole job; return false to cancel. Called from the thread running qremesh.
using QRemeshProgress = std::function<bool(float fraction)>;

// Retopologises `source` (its evaluated positions: sculpt layers are baked; hidden parts are remeshed too)
// into `out`: all quads (as triangle pairs, Mesh::quadPartners), mask and polypaint interpolated from the
// sculpt, polygroups with Keep Groups. False with `error` set on failure or cancel.
bool qremesh(const Mesh& source, const QRemeshOptions& options, Mesh& out, std::string& error,
             const QRemeshProgress& progress = {}, QRemeshStats* stats = nullptr);

// Edge-manifold check used by the tests and the status line: faces as exported (quads counted once).
struct TopologyReport {
    size_t faces = 0, quads = 0, triangles = 0;
    size_t boundaryEdges = 0;     // edges with one face
    size_t nonManifoldEdges = 0;  // edges with more than two faces, or two faces wound the same way
    size_t nonManifoldVertices = 0; // vertices whose faces form more than one fan
    bool manifold() const { return nonManifoldEdges == 0 && nonManifoldVertices == 0; }
    float quadFraction() const { return faces ? float(quads) / float(faces) : 0.0f; }
};
TopologyReport topologyReport(const Mesh& mesh);

// Puts a qremesh result into the active SubTool as one undo step. keepOriginal: first a hidden copy of the
// SubTool as it was goes right below it ("<name> Original"), ready to ProjectAll the detail back from.
// Returns the status message.
std::string applyQRemesh(Document& doc, Mesh result, bool keepOriginal, const QRemeshStats& stats);

// qremesh (core/RetopoCommands.cpp) and project_all (core/Projection.h).
void registerRetopoCommands(CommandRegistry& registry);

} // namespace sf
