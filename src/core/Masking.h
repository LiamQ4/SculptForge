// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush-style masking. Mask strokes are Sculptor::dabMask (see Sculpt.h);
// the whole-mesh operations below edit Mesh::mask in place. They never touch
// fully hidden vertices (except clear, which frees the mask), mark the
// changed vertices dirty for the renderer and return false when nothing
// changed. The mask_* commands wrap them with an undo step.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace sf {

class CommandRegistry;
class ScreenPolygon;

bool maskClear(Mesh& mesh);
bool maskAll(Mesh& mesh);
bool maskInvert(Mesh& mesh);
// Averages each value with its neighbours `iterations` times (softens edges).
bool maskBlur(Mesh& mesh, int iterations);
// Pushes values towards 0 or 1 and tightens soft edges (inverse of blur).
bool maskSharpen(Mesh& mesh, int iterations);
// Grows / shrinks the masked region by one edge ring per iteration.
bool maskGrow(Mesh& mesh, int iterations);
bool maskShrink(Mesh& mesh, int iterations);

// Per-vertex concavity: the offset of the neighbour average along the normal,
// relative to the mean edge length. > 0 in cavities, < 0 on convex areas, ~0 flat.
float vertexConcavity(const Mesh& mesh, uint32_t v);
// Masks concave areas: vertices whose concavity exceeds `threshold` get up to
// `strength` (0-1), with a soft ramp above the threshold. Replaces the mask.
bool maskByCavity(Mesh& mesh, float strength, float threshold);
// Masks every vertex touching a visible triangle of polygroup `group`.
// With `add` the existing mask is kept, otherwise it is replaced.
bool maskByPolygroup(Mesh& mesh, uint32_t group, bool add);

// Mask selections (ZBrush MaskLasso / MaskRect): visible vertices that
// project inside `shape` (NDC under `viewProj`, see ScreenSelect.h) gain
// `strength` (0-1) of mask, or lose it with `unmask`. Vertices behind the
// camera count as outside; the far side of the model is selected too.
bool maskInShape(Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape, float strength, bool unmask);

// Registers mask_clear, mask_all, mask_invert, mask_blur, mask_sharpen,
// mask_grow, mask_shrink, mask_by_cavity, mask_by_polygroup, mask_rect and
// mask_lasso.
void registerMaskingCommands(CommandRegistry& registry);

} // namespace sf
