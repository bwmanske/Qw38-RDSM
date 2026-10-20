#pragma once

// A minimal, dependency-free unit test harness for RDSM.
//
// Define a test with TEST(name) { ... } and assert with CHECK / CHECK_EQ.
// Every test file is compiled into the RDSMTests executable; main() (in
// main.cpp) runs each registered test and prints a [ RUN ]/[ OK ]/[ FAIL ]
// line per test. The process exit code is 0 when every test passes and 1
// otherwise, which is what CTest uses to mark the test passed or failed.
//
// This is intentionally tiny (no external framework) so the test build has no
// downloads and no extra dependencies, matching how nlohmann/json is vendored.

#include <cstdio>
#include <vector>

namespace testfw {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

// Constructed at static-init time by the TEST macro to register a test.
struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

inline void reportFailure(const char* expr, const char* file, int line) {
    ++failureCount();
    std::printf("    FAILED: %s  (%s:%d)\n", expr, file, line);
}

// Runs every registered test. Returns 0 if all pass, 1 otherwise.
inline int runAll() {
    int failedTests = 0;
    for (const TestCase& tc : registry()) {
        const int before = failureCount();
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.fn();
        if (failureCount() == before) {
            std::printf("[  OK  ] %s\n", tc.name);
        } else {
            std::printf("[ FAIL ] %s\n", tc.name);
            ++failedTests;
        }
    }
    std::printf("\n%d test(s) run, %d assertion failure(s), %d failed test(s)\n",
                static_cast<int>(registry().size()), failureCount(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST(name)                                                        \
    static void rdsmon_test_fn_##name();                                  \
    static ::testfw::Registrar rdsmon_test_reg_##name(#name,              \
                                                      &rdsmon_test_fn_##name); \
    static void rdsmon_test_fn_##name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ::testfw::reportFailure(#cond, __FILE__, __LINE__);           \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        if (!((a) == (b))) {                                              \
            ::testfw::reportFailure(#a " == " #b, __FILE__, __LINE__);    \
        }                                                                 \
    } while (0)
