// The harness itself: CHECK_EQ copies its operands (no dangling reference to a temporary), the
// throw checks see the exception type, and the runner filters and reports failures.
#include "lapilli/test/check.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string make(const char* text) { return std::string(text); }
} // namespace

LAPILLI_TEST("CHECK_EQ works on temporaries and reports both values") {
    LAPILLI_CHECK_EQ(make("abc").substr(1), std::string("bc"));
    bool failed = false;
    try {
        LAPILLI_CHECK_EQ(make("x"), std::string("y"));
    } catch (const lapilli::test::Failure& failure) {
        failed = std::string(failure.what()).find("actual:   x") != std::string::npos;
    }
    LAPILLI_CHECK(failed);
}

LAPILLI_TEST("CHECK_THROWS and CHECK_THROWS_AS") {
    LAPILLI_CHECK_THROWS(throw 1);
    LAPILLI_CHECK_THROWS_AS(throw std::invalid_argument("x"), std::logic_error);
    bool caught = false;
    try {
        LAPILLI_CHECK_THROWS((void)0);
    } catch (const lapilli::test::Failure&) {
        caught = true;
    }
    LAPILLI_CHECK(caught);
}

LAPILLI_TEST("the runner filters by name and returns non-zero when nothing ran") {
    std::ostringstream out;
    LAPILLI_CHECK_EQ(lapilli::test::run_all("no case has this name", out), 1);
    LAPILLI_CHECK(out.str().find("# tests 0 fail 0") != std::string::npos);
    std::ostringstream one;
    LAPILLI_CHECK_EQ(lapilli::test::run_all("CHECK_THROWS and", one), 0);
    LAPILLI_CHECK(one.str().find("ok - CHECK_THROWS and CHECK_THROWS_AS") != std::string::npos);
}
