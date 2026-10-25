// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs every TEST registered by the tests/test_*.cpp files.
#include "TestHarness.h"

#include <cstdio>
#include <cstring>
#include <exception>

// sculptforge_tests [part]: only the tests whose name contains `part`.
int main(int argc, char** argv) {
    using namespace sf::test;
    for (const auto& t : registry()) {
        if (argc > 1 && !std::strstr(t.name, argv[1])) continue;
        std::printf("[ RUN] %s\n", t.name);
        std::fflush(stdout); // so a crash still shows which test was running
        int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("  EXCEPTION: %s\n", e.what());
        }
        std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name);
        std::fflush(stdout);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
