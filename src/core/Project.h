// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native project files (.sfproj): an uncompressed ZIP holding a JSON manifest
// ("manifest.json") and little-endian binary blobs for every SubTool (mesh,
// optional attributes, sculpt layers) and any loaded Gaussian splats.
//
// The manifest names every blob with its element type and count, so readers
// ignore anything they don't know (forward compatible) and refuse files whose
// `version` is newer than kProjectVersion with a clear message.
#pragma once

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Image.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

// 1: one mesh. 2: a list of SubTools (version 1 files still load, as one SubTool). 3: SubTools may carry a
// painted texture with layers ("texture"; older files still load).
constexpr int kProjectVersion = 3;
constexpr const char* kProjectExtension = ".sfproj";

// True for paths ending in .sfproj (any case).
bool isProjectPath(const std::string& path);

// Writes `doc` (every SubTool with its name, visibility, mesh, attributes,
// layers, active layer and GoZ name; the active SubTool; splats, brush library
// and doc.projectSettings) to `path`. Does not change the document.
bool saveProject(const std::string& path, const Document& doc, std::string& error);
// Replaces the scene with the project at `path`. On failure `doc` is untouched.
// Restores the active SubTool, sets doc.path, gives every SubTool an empty
// edit history and marks the document clean.
bool loadProject(const std::string& path, Document& doc, std::string& error);
// --- Thumbnails: a small PNG preview ("thumbnail.png") stored right after the manifest, so the
// LightBox can show a project without loading it. Optional: older projects have none.

constexpr int kProjectThumbnailSize = 256;
// Called by saveProject on the saving thread to render the preview (the app renders the current
// view; the CLI sets none). Returning false or an empty image saves no thumbnail.
using ProjectThumbnailProvider = std::function<bool(const Document& doc, Image& out)>;
void setProjectThumbnailProvider(ProjectThumbnailProvider provider);
// Reads only the thumbnail of the project at `path` (seeks past the other entries). False if the
// file is not a project or has no thumbnail; `error` says which.
bool loadProjectThumbnail(const std::string& path, Image& out, std::string& error);

// loadProject wrapped as a command result (open_project, and `open` for .sfproj paths).
CommandResult openProjectCommand(Document& doc, const std::string& path);

// save_project / open_project (called by the CommandRegistry constructor).
void registerProjectCommands(CommandRegistry& registry);

// --- Recent files (pure list logic; the app persists the list) ---

constexpr size_t kMaxRecentFiles = 10;
// Moves `path` to the front (adding it if new), keeping at most `limit` entries.
std::vector<std::string> addRecentFile(std::vector<std::string> list, const std::string& path,
                                       size_t limit = kMaxRecentFiles);
// Drops entries for which `exists` is false, and empty or duplicate entries.
std::vector<std::string> pruneRecentFiles(std::vector<std::string> list,
                                          const std::function<bool(const std::string&)>& exists);

} // namespace sf
