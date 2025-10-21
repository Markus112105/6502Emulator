#pragma once

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace catch2_lite {

// AssertionError carries context about a failed expectation so the runner can
// surface actionable diagnostics without stopping the entire suite.
class AssertionError : public std::exception {
public:
    AssertionError(const char* file, int line, const char* expression)
        : message_(build_message(file, line, expression)) {}

    const char* what() const noexcept override { return message_.c_str(); }

private:
    static std::string build_message(const char* file, int line, const char* expression) {
        std::ostringstream stream;
        stream << file << ':' << line << ": Requirement failed: " << expression;
        return stream.str();
    }

    std::string message_;
};

using TestFunction = void (*)();

struct TestCase {
    std::string name;
    TestFunction function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> instances;
    return instances;
}

class AutoReg {
public:
    AutoReg(const char* name, TestFunction function) {
        registry().push_back(TestCase{name, function});
    }
};

inline int run_all() {
    std::size_t failures = 0;
    const auto& cases = registry();
    std::cout << "Running " << cases.size() << " test case(s)" << std::endl;
    for (const auto& test : cases) {
        try {
            test.function();
            std::cout << "[ OK ] " << test.name << std::endl;
        } catch (const AssertionError& error) {
            ++failures;
            std::cout << "[FAIL] " << test.name << std::endl;
            std::cout << "       " << error.what() << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << test.name << std::endl;
            std::cout << "       Uncaught exception: " << error.what() << std::endl;
        } catch (...) {
            ++failures;
            std::cout << "[FAIL] " << test.name << std::endl;
            std::cout << "       Uncaught non-standard exception" << std::endl;
        }
    }

    const std::size_t passed = cases.size() - failures;
    std::cout << passed << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

}  // namespace catch2_lite

#define CATCH_INTERNAL_JOIN_IMPL(x, y) x##y
#define CATCH_INTERNAL_JOIN(x, y) CATCH_INTERNAL_JOIN_IMPL(x, y)

#define TEST_CASE(name, ...)                                                                 \
    static void CATCH_INTERNAL_JOIN(catch2_test_, __LINE__)();                               \
    static ::catch2_lite::AutoReg CATCH_INTERNAL_JOIN(catch2_reg_, __LINE__)(name,           \
                                                                                   &CATCH_INTERNAL_JOIN(catch2_test_, __LINE__)); \
    static void CATCH_INTERNAL_JOIN(catch2_test_, __LINE__)()

#define REQUIRE(expression)                                                                  \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            throw ::catch2_lite::AssertionError(__FILE__, __LINE__, #expression);            \
        }                                                                                    \
    } while (false)

#ifdef CATCH_CONFIG_MAIN
int main() {
    return ::catch2_lite::run_all();
}
#endif
