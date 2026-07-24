#pragma once

// GTAVR minimal unit-test framework.
// No external dependencies: a TEST() auto-registration macro, CHECK /
// CHECK_NEAR assertions, per-test [PASS]/[FAIL]/[SKIP] output, and a process
// exit code of 1 when any test failed (0 otherwise).
//
// Usage:
//   #include "TestFramework.hpp"
//   TEST(MyFeature_DoesTheThing) {
//       CHECK(1 + 1 == 2);
//       CHECK_NEAR(ComputedValue(), 3.14f, 1e-5f);
//   }
// A test may call SKIP("reason") to report itself as skipped instead of
// pass/fail (used for EXPECTED-PENDING suites).

#include <cstdio>
#include <cmath>
#include <vector>

namespace gtavr {
namespace test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& CheckCount() { static int n = 0; return n; }
inline int& CheckFailureCount() { static int n = 0; return n; }
inline bool& CurrentTestFailed() { static bool f = false; return f; }
inline const char*& CurrentSkipReason() { static const char* r = nullptr; return r; }

inline bool RegisterTest(const char* name, void (*fn)()) {
    Registry().push_back({name, fn});
    return true;
}

inline void ReportCheckPass() {
    ++CheckCount();
}

inline void ReportCheckFailure(const char* file, int line, const char* expr) {
    ++CheckCount();
    ++CheckFailureCount();
    CurrentTestFailed() = true;
    std::printf("    FAILED CHECK: %s  (%s:%d)\n", expr, file, line);
}

inline void ReportCheckNearFailure(const char* file, int line,
                                   const char* exprA, const char* exprB,
                                   double actual, double expected, double eps) {
    ++CheckCount();
    ++CheckFailureCount();
    CurrentTestFailed() = true;
    std::printf("    FAILED CHECK: %s ~= %s (actual %.9g, expected %.9g, eps %.3g)  (%s:%d)\n",
                exprA, exprB, actual, expected, eps, file, line);
}

inline void SetSkipReason(const char* reason) {
    CurrentSkipReason() = reason;
}

// Runs every registered test, prints [PASS]/[FAIL]/[SKIP] per test and a
// summary. Returns 0 when nothing failed, 1 otherwise.
inline int RunAllTests() {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const TestCase& tc : Registry()) {
        CurrentTestFailed() = false;
        CurrentSkipReason() = nullptr;

        tc.fn();

        if (CurrentSkipReason() != nullptr) {
            std::printf("[SKIP] %s: %s\n", tc.name, CurrentSkipReason());
            ++skipped;
        } else if (CurrentTestFailed()) {
            std::printf("[FAIL] %s\n", tc.name);
            ++failed;
        } else {
            std::printf("[PASS] %s\n", tc.name);
            ++passed;
        }
    }

    std::printf("\n%zu test(s): %d passed, %d failed, %d skipped "
                "(%d checks, %d check failures)\n",
                Registry().size(), passed, failed, skipped,
                CheckCount(), CheckFailureCount());
    return failed == 0 ? 0 : 1;
}

} // namespace test
} // namespace gtavr

#define TEST(name)                                                          \
    static void name();                                                     \
    namespace {                                                             \
    const bool name##_registered_ = ::gtavr::test::RegisterTest(#name, &name); \
    }                                                                       \
    static void name()

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (cond) {                                                         \
            ::gtavr::test::ReportCheckPass();                               \
        } else {                                                            \
            ::gtavr::test::ReportCheckFailure(__FILE__, __LINE__, #cond);   \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                   \
    do {                                                                    \
        const double gtavr_a_ = static_cast<double>(actual);                \
        const double gtavr_b_ = static_cast<double>(expected);              \
        const double gtavr_e_ = static_cast<double>(eps);                   \
        if (std::fabs(gtavr_a_ - gtavr_b_) <= gtavr_e_) {                   \
            ::gtavr::test::ReportCheckPass();                               \
        } else {                                                            \
            ::gtavr::test::ReportCheckNearFailure(__FILE__, __LINE__,       \
                #actual, #expected, gtavr_a_, gtavr_b_, gtavr_e_);          \
        }                                                                   \
    } while (0)

// Marks the current test as skipped and returns from its body.
#define SKIP(reason)                                                        \
    do {                                                                    \
        ::gtavr::test::SetSkipReason(reason);                               \
        return;                                                             \
    } while (0)
