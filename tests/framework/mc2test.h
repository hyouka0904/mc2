#pragma once

// Minimal self-contained unit test framework (no external dependencies).
//
//   TEST(group_name) { CHECK(x == 1); CHECK_EQ(a, b); }
//
// Tests register themselves at static-init time; mc2test_main.cpp runs them
// all (or only those whose name contains argv[1]) and returns non-zero if any
// check failed, which is what CTest looks at.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace mc2test {

typedef void (*TestFn)();

struct TestCase {
    const char* name;
    TestFn fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, TestFn fn) { registry().push_back(TestCase{name, fn}); }
};

inline int& failures()
{
    static int count = 0;
    return count;
}

inline void reportFailure(const char* file, int line, const std::string& what)
{
    ++failures();
    fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, what.c_str());
}

template <typename T>
std::string toString(const T& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

inline std::string toString(const unsigned char& v) { return toString((int)v); }
inline std::string toString(const signed char& v) { return toString((int)v); }

} // namespace mc2test

#define MC2TEST_CONCAT2(a, b) a##b
#define MC2TEST_CONCAT(a, b) MC2TEST_CONCAT2(a, b)

#define TEST(name)                                                              \
    static void MC2TEST_CONCAT(test_fn_, name)();                               \
    static mc2test::Registrar MC2TEST_CONCAT(test_reg_, name)(#name,            \
        &MC2TEST_CONCAT(test_fn_, name));                                       \
    static void MC2TEST_CONCAT(test_fn_, name)()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond))                                                            \
            mc2test::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");     \
    } while (0)

#define CHECK_EQ(actual, expected)                                              \
    do {                                                                        \
        const auto& mc2_a = (actual);                                           \
        const auto& mc2_e = (expected);                                         \
        if (!(mc2_a == mc2_e))                                                  \
            mc2test::reportFailure(__FILE__, __LINE__,                          \
                "CHECK_EQ(" #actual ", " #expected "): got " +                  \
                mc2test::toString(mc2_a) + ", expected " +                      \
                mc2test::toString(mc2_e));                                      \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                       \
    do {                                                                        \
        const double mc2_a = (double)(actual);                                  \
        const double mc2_e = (double)(expected);                                \
        if (!(std::fabs(mc2_a - mc2_e) <= (eps)))                               \
            mc2test::reportFailure(__FILE__, __LINE__,                          \
                "CHECK_NEAR(" #actual ", " #expected "): got " +                \
                mc2test::toString(mc2_a) + ", expected " +                      \
                mc2test::toString(mc2_e));                                      \
    } while (0)

// Stops the current test (e.g. before dereferencing a pointer that was null).
#define REQUIRE(cond)                                                           \
    do {                                                                        \
        if (!(cond)) {                                                          \
            mc2test::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");   \
            return;                                                             \
        }                                                                       \
    } while (0)
