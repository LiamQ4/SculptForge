// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dependency-free test harness. Each tests/test_*.cpp file includes this and
// defines TEST(name) { CHECK(...); } cases; tests/test_main.cpp runs them all.
// New test files are picked up by CMake automatically (re-run cmake).
#pragma once

#include "core/Document.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sf::test {

inline int g_failures = 0;
inline int g_checks = 0;

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Register {
    Register(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline Document makeDoc(Mesh m) {
    Document d;
    d.setMesh(std::move(m));
    return d;
}

inline std::string tempPath(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

} // namespace sf::test

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++::sf::test::g_checks;                                                         \
        if (!(cond)) {                                                                  \
            ++::sf::test::g_failures;                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define TEST(name)                                                  \
    static void name();                                             \
    static ::sf::test::Register reg_##name(#name, name);            \
    static void name()

using sf::test::makeDoc;
using sf::test::tempPath;
