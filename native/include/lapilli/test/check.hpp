// Minimal registry test harness for Lapilli and its consumers (no test framework dependency).
// Each LAPILLI_TEST registers a case; the runner (Lapilli::test_main, src/test/main.cpp) runs all
// of them, or those whose name contains argv[1], prints TAP-like lines and returns non-zero when a
// case failed or none ran.
//
//   LAPILLI_TEST("name") { LAPILLI_CHECK(cond); LAPILLI_CHECK_EQ(actual, expected); }
//
// LAPILLI_CHECK_EQ copies both values before comparing (never binds a reference to a temporary
// that dies at the end of the full expression).
#pragma once

#include <functional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lapilli::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

/** All registered cases, in registration order (defined in the runner). */
std::vector<Case>& registry();

/** Runs the cases whose name contains `filter` (empty = all). Returns the process exit code. */
int run_all(const std::string& filter, std::ostream& out);

struct Register {
    Register(std::string name, std::function<void()> body) { registry().push_back({std::move(name), std::move(body)}); }
};

class Failure : public std::runtime_error {
public:
    explicit Failure(const std::string& what) : std::runtime_error(what) {}
};

template <class T>
std::string show(const T& value) {
    if constexpr (requires(std::ostream& out, const T& v) { out << v; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<value>";
    }
}

inline std::string where(const char* file, int line) { return std::string(file) + ":" + std::to_string(line) + ": "; }

} // namespace lapilli::test

#define LAPILLI_TEST_CONCAT2(a, b) a##b
#define LAPILLI_TEST_CONCAT(a, b) LAPILLI_TEST_CONCAT2(a, b)
#define LAPILLI_TEST(name)                                                                                          \
    static void LAPILLI_TEST_CONCAT(lapilli_test_body_, __LINE__)();                                                \
    static const ::lapilli::test::Register LAPILLI_TEST_CONCAT(lapilli_test_reg_, __LINE__)(                         \
        name, &LAPILLI_TEST_CONCAT(lapilli_test_body_, __LINE__));                                                  \
    static void LAPILLI_TEST_CONCAT(lapilli_test_body_, __LINE__)()

#define LAPILLI_CHECK(cond)                                                                                         \
    do {                                                                                                            \
        if (!(cond)) throw ::lapilli::test::Failure(::lapilli::test::where(__FILE__, __LINE__) + "CHECK(" #cond ")"); \
    } while (0)

#define LAPILLI_CHECK_EQ(actual, expected)                                                                          \
    do {                                                                                                            \
        const auto lapilli_a_ = (actual);                                                                           \
        const auto lapilli_e_ = (expected);                                                                         \
        if (!(lapilli_a_ == lapilli_e_))                                                                            \
            throw ::lapilli::test::Failure(::lapilli::test::where(__FILE__, __LINE__) + #actual " == " #expected    \
                                           "\n  actual:   " + ::lapilli::test::show(lapilli_a_) +                   \
                                           "\n  expected: " + ::lapilli::test::show(lapilli_e_));                   \
    } while (0)

#define LAPILLI_CHECK_THROWS(expr)                                                                                  \
    do {                                                                                                            \
        bool lapilli_threw_ = false;                                                                                \
        try {                                                                                                       \
            (void)(expr);                                                                                           \
        } catch (...) {                                                                                             \
            lapilli_threw_ = true;                                                                                  \
        }                                                                                                           \
        if (!lapilli_threw_)                                                                                        \
            throw ::lapilli::test::Failure(::lapilli::test::where(__FILE__, __LINE__) + "expected a throw: " #expr); \
    } while (0)

#define LAPILLI_CHECK_THROWS_AS(expr, type)                                                                         \
    do {                                                                                                            \
        bool lapilli_threw_ = false;                                                                                \
        try {                                                                                                       \
            (void)(expr);                                                                                           \
        } catch (const type&) {                                                                                     \
            lapilli_threw_ = true;                                                                                  \
        }                                                                                                           \
        if (!lapilli_threw_)                                                                                        \
            throw ::lapilli::test::Failure(::lapilli::test::where(__FILE__, __LINE__) + "expected " #type ": " #expr); \
    } while (0)
