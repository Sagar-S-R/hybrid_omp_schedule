#pragma once
/// @file minitest.hpp
/// @brief Tiny self-contained test framework — no external dependencies.

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> reg;
    return reg;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct SkipException {
    std::string reason;
};

struct FailException {
    std::string msg;
};

inline int run_all(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.substr(0, 9) == "--filter=") {
            filter = arg.substr(9);
        } else if (arg == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& tc : registry()) {
        if (!filter.empty() && tc.name.find(filter) == std::string::npos) continue;
        std::cout << "[ RUN  ] " << tc.name << '\n';
        try {
            tc.fn();
            std::cout << "[ PASS ] " << tc.name << '\n';
            ++passed;
        } catch (const SkipException& e) {
            std::cout << "[ SKIP ] " << tc.name << " : " << e.reason << '\n';
            ++skipped;
        } catch (const FailException& e) {
            std::cout << "[ FAIL ] " << tc.name << " : " << e.msg << '\n';
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[ FAIL ] " << tc.name << " : EXCEPTION: " << e.what() << '\n';
            ++failed;
        } catch (...) {
            std::cout << "[ FAIL ] " << tc.name << " : UNKNOWN EXCEPTION\n";
            ++failed;
        }
    }
    std::cout << "\n=== Results: " << passed << " passed, "
              << failed << " failed, " << skipped << " skipped ===\n";
    return (failed == 0) ? 0 : 1;
}

} // namespace minitest

// ── Two-level macro to force __LINE__ expansion before token pasting ──────────
#define MINITEST_CONCAT_INNER(a, b) a##b
#define MINITEST_CONCAT(a, b) MINITEST_CONCAT_INNER(a, b)

#define TEST(name)                                                               \
    static void MINITEST_CONCAT(_test_fn_, __LINE__)();                          \
    static ::minitest::Registrar MINITEST_CONCAT(_reg_, __LINE__)(                \
        name, MINITEST_CONCAT(_test_fn_, __LINE__));                              \
    static void MINITEST_CONCAT(_test_fn_, __LINE__)()

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__ << ": CHECK(" #expr ") failed"; \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        if (!((a) == (b))) {                                                     \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__                                  \
                 << ": CHECK_EQ(" #a ", " #b ") failed: "                        \
                 << (a) << " != " << (b);                                        \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define CHECK_NE(a, b)                                                           \
    do {                                                                         \
        if ((a) == (b)) {                                                        \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__                                  \
                 << ": CHECK_NE(" #a ", " #b ") failed: both == " << (a);       \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        const double _a = static_cast<double>(a);                                \
        const double _b = static_cast<double>(b);                                \
        const double _t = static_cast<double>(tol);                              \
        if (std::abs(_a - _b) > _t) {                                            \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__                                  \
                 << ": CHECK_NEAR(" #a ", " #b ", " #tol ") failed: |"           \
                 << _a << " - " << _b << "| = " << std::abs(_a - _b)            \
                 << " > " << _t;                                                 \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define CHECK_GT(a, b)                                                           \
    do {                                                                         \
        if (!((a) > (b))) {                                                      \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__                                  \
                 << ": CHECK_GT(" #a ", " #b ") failed: "                        \
                 << (a) << " <= " << (b);                                        \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define CHECK_GE(a, b)                                                           \
    do {                                                                         \
        if (!((a) >= (b))) {                                                     \
            std::ostringstream _oss;                                             \
            _oss << __FILE__ << ':' << __LINE__                                  \
                 << ": CHECK_GE(" #a ", " #b ") failed: "                        \
                 << (a) << " < " << (b);                                         \
            throw ::minitest::FailException{_oss.str()};                         \
        }                                                                        \
    } while (false)

#define SKIP(reason)                                                             \
    throw ::minitest::SkipException{reason}
