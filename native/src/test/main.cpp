#include "lapilli/test/check.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace lapilli::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

int run_all(const std::string& filter, std::ostream& out) {
    int failed = 0;
    int ran = 0;
    for (const auto& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        ++ran;
        try {
            test.body();
            out << "ok - " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            out << "not ok - " << test.name << "\n  " << error.what() << '\n';
        } catch (...) {
            ++failed;
            out << "not ok - " << test.name << "\n  unknown exception\n";
        }
    }
    out << "# tests " << ran << " fail " << failed << '\n';
    out.flush();
    return failed == 0 && ran > 0 ? 0 : 1;
}

} // namespace lapilli::test

int main(int argc, char** argv) { return lapilli::test::run_all(argc > 1 ? argv[1] : "", std::cout); }
