// CKW: launch the installed Huion app at normal user privilege, if needed.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>
#endif

namespace sf {
void App::startCkwHuion() {
#if defined(_WIN32)
    if (!options_.uiTest.empty() || !options_.recordPath.empty() || !options_.renderPath.empty()) return;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        SDL_Log("CKW Huion: could not inspect running processes; skipping auto-start");
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"HuionTablet.exe") == 0) { running = true; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (running) { SDL_Log("CKW Huion: HuionTablet is already running"); return; }
    wchar_t programFiles[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"ProgramFiles", programFiles, DWORD(std::size(programFiles)));
    if (size == 0 || size >= std::size(programFiles)) return;
    const auto folder = std::filesystem::path(programFiles) / L"HuionTablet";
    const auto exe = folder / L"HuionTablet.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(exe, error)) {
        SDL_Log("CKW Huion: HuionTablet is not installed in Program Files; skipping auto-start");
        return;
    }
    std::wstring commandLine = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION process{};
    // CreateProcess never requests elevation. Do not use runas, change compatibility
    // flags, create an elevated scheduled task, or suppress a security prompt.
    if (CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, folder.c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        SDL_Log("CKW Huion: launched HuionTablet; Huion itself may request administrator consent");
    } else {
        SDL_Log("CKW Huion: normal launch failed (Windows error %lu); no elevation attempted", GetLastError());
    }
#endif
}
} // namespace sf
