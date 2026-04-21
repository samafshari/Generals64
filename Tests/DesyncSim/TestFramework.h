// TestFramework.h
//
// Minimal, header-only test framework for the DesyncSim suite.
//
// Design mirrors Tests/ShadowMathTests.cpp: a global TestState, a Section()
// function that prints a separator and records the current section name, CHECK
// macros that print pass/fail lines and tally results, and a FinalReport()
// helper that prints the summary and returns the failure count (which main()
// can use directly as its exit code).
//
// No external dependencies. C++20 but only uses basic std-lib.
//
// Usage pattern:
//   int main() {
//       Section("My section");
//       CHECK(1 + 1 == 2);
//       CHECK_EQ(someInt, 42);
//       CHECK_CRC_MATCH(crcA, crcB, "frame 7 state");
//       return FinalReport();
//   }

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// ----------------------------------------------------------------------------
// Global state
// ----------------------------------------------------------------------------

struct TestState
{
    int         total   = 0;
    int         failed  = 0;
    std::string currentSection;
};

// One global instance, visible to all translation units that include this header.
// If multiple TUs include this header in the same executable, declare it extern
// in all but one. For single-file tests (like the ones here) this is fine as-is.
static TestState g_tests;

// ----------------------------------------------------------------------------
// Section marker — groups related checks in output
// ----------------------------------------------------------------------------

static void Section(const char* name)
{
    g_tests.currentSection = name;
    std::printf("\n=== %s ===\n", name);
}

// ----------------------------------------------------------------------------
// CHECK — boolean condition
// ----------------------------------------------------------------------------

#define CHECK(cond) do {                                                             \
    ++g_tests.total;                                                                 \
    if (!(cond)) {                                                                   \
        ++g_tests.failed;                                                            \
        std::printf("  FAIL  [%s] %s:%d  %s\n",                                     \
            g_tests.currentSection.c_str(), __FILE__, __LINE__, #cond);              \
    } else {                                                                         \
        std::printf("  ok    %s\n", #cond);                                          \
    }                                                                                \
} while (0)

// ----------------------------------------------------------------------------
// CHECK_EQ — two values that must be equal; prints both on failure
// ----------------------------------------------------------------------------

#define CHECK_EQ(a, b) do {                                                          \
    auto _a = (a);                                                                   \
    auto _b = (b);                                                                   \
    ++g_tests.total;                                                                 \
    if (!(_a == _b)) {                                                               \
        ++g_tests.failed;                                                            \
        std::printf("  FAIL  [%s] %s:%d  %s != %s\n",                               \
            g_tests.currentSection.c_str(), __FILE__, __LINE__, #a, #b);             \
    } else {                                                                         \
        std::printf("  ok    %s == %s\n", #a, #b);                                  \
    }                                                                                \
} while (0)

// ----------------------------------------------------------------------------
// CHECK_CRC_MATCH — two uint32_t CRC values with a human-readable label.
// Prints the hex values on failure so the developer can see which CRC won.
// This is the core assertion for the lockstep harness.
// ----------------------------------------------------------------------------

#define CHECK_CRC_MATCH(a, b, label) do {                                            \
    uint32_t _a = static_cast<uint32_t>(a);                                          \
    uint32_t _b = static_cast<uint32_t>(b);                                          \
    ++g_tests.total;                                                                 \
    if (_a != _b) {                                                                  \
        ++g_tests.failed;                                                            \
        std::printf("  FAIL  [%s] %s:%d  CRC mismatch for \"%s\":"                  \
                    "  0x%08X  !=  0x%08X\n",                                        \
            g_tests.currentSection.c_str(), __FILE__, __LINE__,                      \
            (label), _a, _b);                                                        \
    } else {                                                                         \
        std::printf("  ok    CRC match \"%s\" (0x%08X)\n", (label), _a);             \
    }                                                                                \
} while (0)

// ----------------------------------------------------------------------------
// FinalReport — print summary and return failure count for use as exit code
// ----------------------------------------------------------------------------

static int FinalReport()
{
    std::printf("\n====================\n");
    std::printf("  %d / %d  passed\n", g_tests.total - g_tests.failed, g_tests.total);
    if (g_tests.failed > 0)
        std::printf("  %d FAILED\n", g_tests.failed);
    else
        std::printf("  all passed\n");
    std::printf("====================\n");
    return g_tests.failed;
}
