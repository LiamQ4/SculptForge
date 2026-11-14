// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include "app/UserData.h"
#include <cstdlib>

namespace sf {
// SDL's Windows pref path uses the shell API, which ignores APPDATA. UI tests set
// this process-local override before startup; regular launches keep SDL's path.
inline char* appPrefPath() {
    if (const char* path = std::getenv("SF_TEST_SETTINGS_DIR"); path && *path) return SDL_strdup(path);
    const std::string path = userDataDir();
    return path.empty() ? nullptr : SDL_strdup(path.c_str());
}
} // namespace sf
