// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Actions.h"

#include <algorithm>

namespace sf {

// Phase 0: registry storage is real; key handling, overrides and MRU are filled in by the help/console workstream.

void ActionRegistry::add(Action a) {
    for (auto& existing : actions_)
        if (existing.id == a.id) {
            existing = std::move(a);
            return;
        }
    actions_.push_back(std::move(a));
}

const Action* ActionRegistry::find(std::string_view id) const {
    for (const auto& a : actions_)
        if (a.id == id) return &a;
    return nullptr;
}

bool ActionRegistry::handleKey(SDL_Keycode, SDL_Keymod) { return false; }

std::string ActionRegistry::shortcutText(std::string_view id) const {
    for (const auto& [oid, chord] : overrides_)
        if (oid == id) return chord;
    const Action* a = find(id);
    return a ? a->shortcut : std::string();
}

void ActionRegistry::setShortcut(std::string_view id, std::string chord) {
    for (auto& [oid, c] : overrides_)
        if (oid == id) {
            c = std::move(chord);
            return;
        }
    overrides_.emplace_back(std::string(id), std::move(chord));
}

Json ActionRegistry::overridesToJson() const {
    Json j = Json::object();
    for (const auto& [id, chord] : overrides_) j.set(id, chord);
    return j;
}

void ActionRegistry::overridesFromJson(const Json&) {}

void ActionRegistry::touch(std::string_view id) {
    recent_.erase(std::remove(recent_.begin(), recent_.end(), std::string(id)), recent_.end());
    recent_.insert(recent_.begin(), std::string(id));
    if (recent_.size() > 20) recent_.resize(20);
}

} // namespace sf
