#pragma once
// Minimal dependency-free test harness: TEST(name) { CHECK(...); } and a main() that runs all.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace fftest {

struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& Registry() { static std::vector<Case> r; return r; }
inline int& Failures() { static int f = 0; return f; }
inline const char*& Current() { static const char* c = ""; return c; }

struct Registrar { Registrar(const char* n, std::function<void()> f) { Registry().push_back({n, std::move(f)}); } };

inline void Fail(const char* file, int line, const std::string& what)
{
    ++Failures();
    std::printf("  FAIL %s (%s:%d): %s\n", Current(), file, line, what.c_str());
}

inline int RunAll()
{
    int failedCases = 0;
    for (auto& c : Registry()) {
        Current() = c.name;
        const int before = Failures();
        c.fn();
        const bool ok = Failures() == before;
        if (!ok) ++failedCases;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed\n", Registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

} // namespace fftest

#define FF_CAT2(a, b) a##b
#define FF_CAT(a, b) FF_CAT2(a, b)
#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::fftest::Registrar FF_CAT(reg_, name)(#name, &name);                    \
    static void name()
#define CHECK(cond) do { if (!(cond)) ::fftest::Fail(__FILE__, __LINE__, #cond); } while (0)
#define CHECK_NEAR(a, b, tol) do { const double _a = (a), _b = (b);                  \
    if (!(std::fabs(_a - _b) <= (tol))) ::fftest::Fail(__FILE__, __LINE__,           \
        std::string(#a " ~= " #b "  (") + std::to_string(_a) + " vs " + std::to_string(_b) + ")"); } while (0)
#define TEST_MAIN() int main() { return ::fftest::RunAll(); }
