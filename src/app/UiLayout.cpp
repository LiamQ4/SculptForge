// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/UiLayout.h"

namespace sf {

namespace {

Json stringArray(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (const auto& s : v) a.push(s);
    return a;
}

void readStrings(const Json& j, std::vector<std::string>& out) {
    if (!j.isArray()) return;
    out.clear();
    for (size_t i = 0; i < j.size(); ++i)
        if (j[i].isString() && !j[i].asString().empty()) out.push_back(j[i].asString());
}

} // namespace

UiLayout standardLayout() {
    // docs/ui-design.md §3: what a ZBrush user expects to find around the canvas.
    UiLayout l;
    l.rightTray = {"tool"};
    l.openSubpalettes = {"tool.subtool"};
    l.topShelf = {"ui.lightbox",       "sep",           "draw.sculpt",          "draw.paint",          "sep",
                  "draw.zadd",         "draw.zsub",     "draw.rgb",             "sep",                 "widget.draw_size",
                  "widget.strength",   "row",           "ui.brush_lab",         "draw.front_faces",    "sep",
                  "transform.sym_x",   "transform.sym_y", "transform.sym_z",    "sep",                 "draw.pressure_strength",
                  "draw.pressure_size", "widget.points"};
    l.leftShelf = {"widget.tile.brush", "widget.tile.stroke", "widget.tile.alpha", "widget.tile.material", "widget.color"};
    l.rightShelf = {"view.frame", "widget.nav.move", "widget.nav.zoom", "widget.nav.rotate", "sep",
                    "view.polyf", "view.polygroups", "view.mask",       "view.polypaint",    "sep",
                    "subtool.solo", "view.splats"};
    l.bottomShelf = {"brush.standard", "brush.clay",    "brush.claybuildup", "brush.damstandard", "brush.move",
                     "brush.smooth",   "brush.inflate", "brush.pinch",       "brush.flatten",     "brush.crease",
                     "brush.trim",     "brush.snakehook", "brush.layer",     "brush.nudge",       "draw.paint",
                     "widget.add_brush"};
    return l;
}

Json toJson(const UiLayout& l) {
    Json j = Json::object();
    j.set("version", double(l.version));
    j.set("shelvesVisible", l.shelvesVisible);
    j.set("paletteBarVisible", l.paletteBarVisible);
    j.set("leftHanded", l.leftHanded);
    j.set("leftTrayOpen", l.leftTrayOpen);
    j.set("rightTrayOpen", l.rightTrayOpen);
    j.set("leftTrayWidth", double(l.leftTrayWidth));
    j.set("rightTrayWidth", double(l.rightTrayWidth));
    j.set("leftTray", stringArray(l.leftTray));
    j.set("rightTray", stringArray(l.rightTray));
    j.set("openSubpalettes", stringArray(l.openSubpalettes));
    j.set("topShelf", stringArray(l.topShelf));
    j.set("leftShelf", stringArray(l.leftShelf));
    j.set("rightShelf", stringArray(l.rightShelf));
    j.set("bottomShelf", stringArray(l.bottomShelf));
    j.set("consoleOpen", l.consoleOpen);
    j.set("consoleHeight", double(l.consoleHeight));
    j.set("lightboxOpen", l.lightboxOpen);
    j.set("lightboxRows", double(l.lightboxRows));
    j.set("lightboxTab", l.lightboxTab);
    j.set("lightboxBottom", l.lightboxBottom);
    return j;
}

bool fromJson(const Json& j, UiLayout& l, std::string& error) {
    if (!j.isObject()) {
        error = "The UI configuration is not a JSON object";
        return false;
    }
    UiLayout out = standardLayout();
    auto flag = [&](const char* key, bool& v) { v = j[key].asBool(v); };
    auto number = [&](const char* key, float& v) { v = float(j[key].asNumber(v)); };
    flag("shelvesVisible", out.shelvesVisible);
    flag("paletteBarVisible", out.paletteBarVisible);
    flag("leftHanded", out.leftHanded);
    flag("leftTrayOpen", out.leftTrayOpen);
    flag("rightTrayOpen", out.rightTrayOpen);
    number("leftTrayWidth", out.leftTrayWidth);
    number("rightTrayWidth", out.rightTrayWidth);
    readStrings(j["leftTray"], out.leftTray);
    readStrings(j["rightTray"], out.rightTray);
    readStrings(j["openSubpalettes"], out.openSubpalettes);
    readStrings(j["topShelf"], out.topShelf);
    readStrings(j["leftShelf"], out.leftShelf);
    readStrings(j["rightShelf"], out.rightShelf);
    readStrings(j["bottomShelf"], out.bottomShelf);
    flag("consoleOpen", out.consoleOpen);
    number("consoleHeight", out.consoleHeight);
    flag("lightboxOpen", out.lightboxOpen);
    out.lightboxRows = int(j["lightboxRows"].asNumber(out.lightboxRows));
    if (j["lightboxTab"].isString()) out.lightboxTab = j["lightboxTab"].asString();
    flag("lightboxBottom", out.lightboxBottom);
    l = std::move(out);
    return true;
}

} // namespace sf
