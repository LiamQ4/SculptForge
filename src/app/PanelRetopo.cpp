// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometry > QRemesh and SubTool > Project: ZBrush's ZRemesher and ProjectAll. QRemesh (core/QRemesh.h) runs
// on a worker thread with a progress bar and Cancel, then replaces the SubTool as one undo step (optionally
// keeping the original as a hidden SubTool below it). ProjectAll (core/Projection.h) is quick, so it runs
// as the project_all command. The console shows both as script lines.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Ui.h"

#include <algorithm>
#include <thread>

namespace sf {

namespace {

constexpr const char* kQRemeshPath = "Tool > Geometry > QRemesh";
constexpr const char* kProjectPath = "Tool > SubTool > Project";

// Faces as a modeller counts them: quads once, plus loose triangles.
size_t faceCount(const Mesh& m) { return m.triangleCount() - m.quadCount(); }

int symmetryAxis(const Symmetry& s) { return s.y && !s.x ? 1 : s.z && !s.x && !s.y ? 2 : 0; }

} // namespace

void App::registerRetopoUiActions() {
    auto add = [this](const char* id, const char* label, const char* palette, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = palette;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("geometry.qremesh", "QRemesh", kQRemeshPath, [this] { startQRemesh(); },
        [this] { return !qremesh_ && doc_.mesh.triangleCount() > 0; },
        "Automatic quad retopology, like ZBrush's ZRemesher: rebuilds the SubTool as clean, all-quad topology whose "
        "edge loops follow the forms, close to Target Polygons. Runs in the background (Cancel stops it); undoable. "
        "Then divide it and use SubTool > Project > ProjectAll to bring the detail back.",
        "qremesh");
    add("subtool.project_all", "ProjectAll", kProjectPath, [this] {
            runCommand("project_all", {{"distance", double(retopo_.projectDist)}, {"mean", retopo_.projectMean},
                                       {"blur", double(retopo_.projectBlur)}, {"geometry", retopo_.projectGeometry},
                                       {"polypaint", retopo_.projectPolypaint}});
        },
        [this] {
            for (int i = 0; i < doc_.subtoolCount(); ++i)
                if (i != doc_.activeSubtool && doc_.subtools[size_t(i)].visible) return true;
            return false;
        },
        "Project the shape and polypaint of every other visible SubTool onto this one: each vertex moves along its "
        "normal to the nearest surface within Dist. The mask holds vertices back. Typical use: QRemesh a copy, divide "
        "it a few times, show the original, ProjectAll.",
        "project_all");
}

void App::startQRemesh() {
    if (qremesh_ || doc_.mesh.triangleCount() == 0) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    QRemeshOptions o;
    o.targetQuads = std::clamp(retopo_.target, 16, 2'000'000);
    o.adaptive = retopo_.adaptive;
    o.keepGroups = retopo_.keepGroups;
    o.detectEdges = retopo_.detectEdges;
    o.usePolypaint = retopo_.usePolypaint;
    o.symmetryAxis = retopo_.symmetry ? symmetryAxis(symmetry_) : -1;

    auto job = std::make_unique<QRemeshJob>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<Mesh>();
    job->stats = std::make_shared<QRemeshStats>();
    job->error = std::make_shared<std::string>();
    job->subtool = doc_.activeSubtoolInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->keepOriginal = retopo_.keepOriginal;
    job->args = {{"target", double(o.targetQuads)}, {"adaptive", double(o.adaptive)}};
    if (o.keepGroups) job->args["keep_groups"] = true;
    if (!o.detectEdges) job->args["detect_edges"] = false;
    if (o.usePolypaint) job->args["use_polypaint"] = true;
    if (o.symmetryAxis >= 0) job->args["symmetry"] = std::string(1, char('x' + o.symmetryAxis));
    if (job->keepOriginal) job->args["keep_original"] = true;

    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), source = std::make_shared<Mesh>(doc_.mesh), o, progress = job->progress,
                 cancel = job->cancel, result = job->result, stats = job->stats, error = job->error]() mutable {
        const bool ok = qremesh(*source, o, *result, *error, [&](float f) {
            progress->store(f);
            return !cancel->load();
        }, stats.get());
        promise.set_value(ok);
    }).detach();
    qremesh_ = std::move(job);
    status_ = "QRemesh: " + std::to_string(faceCount(doc_.mesh)) + " polygons to about " + std::to_string(o.targetQuads) +
              " quads...";
}

void App::pollQRemesh() {
    if (!qremesh_) return;
    if (qremesh_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "QRemesh: " + std::to_string(int(qremesh_->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<QRemeshJob> job = std::move(qremesh_);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    // The SubTool may be inactive now, or changed topology meanwhile.
    int index = -1;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].id == job->subtool) index = i;
    if (index < 0 || doc_.subtoolMesh(index).triangleCount() != job->triangles) {
        status_ = "QRemesh skipped: the SubTool changed while it was being remeshed";
        return;
    }
    if (index != doc_.activeSubtool) doc_.selectSubtool(index);
    if (doc_.sculptor.strokeActive()) endStroke();
    CommandResult r{true, applyQRemesh(doc_, std::move(*job->result), job->keepOriginal, *job->stats)};
    if (const std::string notice = doc_.takeNotice(); !notice.empty()) r.message += ". " + notice; // levels dropped
    status_ = r.message;
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    updateTitle();
    consoleEcho("qremesh", job->args, r);
}

void App::drawQRemeshContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SeparatorText("QRemesh");
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;

    if (qremesh_) {
        ImGui::ProgressBar(qremesh_->progress->load(), ImVec2(half, 0), "Remeshing...");
        ui::HelpText("QRemesh", nullptr, "Remeshing in the background. The result replaces the SubTool when it's done.",
                     kQRemeshPath);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1, 0))) qremesh_->cancel->store(true);
        ui::HelpText("Cancel", nullptr, "Stop QRemesh; the SubTool stays as it is.", kQRemeshPath);
    } else {
        ui::ActionButton(*this, "geometry.qremesh", ImVec2(half, 0));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu polygons", faceCount(doc_.mesh));
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##qtarget", &retopo_.target, 100, 200000, "Target Polygons %d", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Target Polygons", nullptr,
                 "About how many quads QRemesh makes (Ctrl+click to type a number). Half / Same / Double set it from "
                 "the SubTool's current polygon count.",
                 kQRemeshPath);
    const size_t faces = faceCount(doc_.mesh);
    auto setTarget = [&](double k) { retopo_.target = int(std::clamp(double(faces) * k, 100.0, 2'000'000.0)); };
    if (ImGui::Button("Half", ImVec2(third, 0))) setTarget(0.5);
    ui::HelpText("Half", nullptr, "Target half the SubTool's current polygon count.", kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button("Same", ImVec2(third, 0))) setTarget(1.0);
    ui::HelpText("Same", nullptr, "Target the SubTool's current polygon count (to clean up topology without changing its density).",
                 kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button("Double", ImVec2(-1, 0))) setTarget(2.0);
    ui::HelpText("Double", nullptr, "Target twice the SubTool's current polygon count.", kQRemeshPath);

    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##qadaptive", &retopo_.adaptive, 0.0f, 1.0f, "Adaptive Size %.2f");
    ui::HelpText("Adaptive Size", nullptr,
                 "0: quads all about the same size. Higher: smaller quads where the surface curves, bigger ones on "
                 "flat areas (the count stays near the target).",
                 kQRemeshPath);

    ImGui::Checkbox("Keep Groups", &retopo_.keepGroups);
    ui::HelpText("Keep Groups", nullptr,
                 "Edge loops follow the polygroup borders exactly and the result keeps the polygroups. Paint groups "
                 "where you want loops (around the eyes, the mouth) to guide the flow.",
                 kQRemeshPath);
    ImGui::SameLine(half + style.ItemSpacing.x);
    ImGui::Checkbox("Detect Edges", &retopo_.detectEdges);
    ui::HelpText("Detect Edges", nullptr, "Hard edges (faces meeting at more than 50 degrees) get an edge loop, so they stay sharp.",
                 kQRemeshPath);
    ImGui::Checkbox("Use Polypaint", &retopo_.usePolypaint);
    ui::HelpText("Use Polypaint", nullptr,
                 "Paint where you want density: red areas get up to 4 times the quads, blue areas down to a quarter, "
                 "white is unchanged.",
                 kQRemeshPath);
    ImGui::SameLine(half + style.ItemSpacing.x);
    ImGui::Checkbox("Symmetry", &retopo_.symmetry);
    const char axis = char('X' + symmetryAxis(symmetry_));
    const std::string symHelp = std::string("Mirror the topology across the ") + axis +
                                " plane through the origin (the Transform palette's symmetry axis), welded on the seam.";
    ui::HelpText("Symmetry", nullptr, symHelp.c_str(), kQRemeshPath);
    ImGui::Checkbox("Keep Original", &retopo_.keepOriginal);
    ui::HelpText("Keep Original", nullptr,
                 "Keep the SubTool as it was, hidden, right below the new one (\"<name> Original\"), ready to "
                 "ProjectAll its detail back. Off: replace it, as ZBrush does (undo brings it back).",
                 kQRemeshPath);
}

void App::drawProjectContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SeparatorText("Project");
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "subtool.project_all", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##pdist", &retopo_.projectDist, 0.001f, 0.2f, "Dist %.3f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Dist", nullptr,
                 "How far each vertex looks for the source surface, along its normal, as a fraction of the SubTool's "
                 "size. Raise it if parts don't pick up the detail; lower it where surfaces are close together.",
                 kProjectPath);
    ImGui::SetNextItemWidth(half);
    ImGui::SliderInt("##pblur", &retopo_.projectBlur, 0, 10, "PA Blur %d");
    ui::HelpText("PA Blur", nullptr, "Smooth the projected result this many times (softens noise from the source).",
                 kProjectPath);
    ImGui::SameLine();
    ImGui::Checkbox("Mean", &retopo_.projectMean);
    ui::HelpText("Mean", nullptr,
                 "Where the source is found on both sides of a vertex, settle halfway between instead of on the nearest.",
                 kProjectPath);
    ImGui::Checkbox("Geometry", &retopo_.projectGeometry);
    ui::HelpText("Geometry", nullptr, "Project the shape (into the active sculpt layer).", kProjectPath);
    ImGui::SameLine(half + style.ItemSpacing.x);
    ImGui::Checkbox("Polypaint", &retopo_.projectPolypaint);
    ui::HelpText("Polypaint", nullptr, "Project the polypaint (into the active paint layer) when a source has any.",
                 kProjectPath);
}

} // namespace sf
