// Entry point for the GTAVR unit-test executable.
// Test suites self-register via the TEST() macro in TestFramework.hpp.

#include "TestFramework.hpp"

int main() {
    return ::gtavr::test::RunAllTests();
}
