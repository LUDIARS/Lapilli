// lapilli::js_math against values recorded from Node v22.
#include "lapilli/js_math.hpp"
#include "lapilli/test/check.hpp"

#include <cmath>

using namespace lapilli::js_math;

LAPILLI_TEST("toFixed equals Number#toFixed") {
    const struct {
        double v;
        int d;
        const char* text;
    } cases[] = {
        {2.5, 0, "3"}, {1.005, 2, "1.00"}, {-1.5, 0, "-2"}, {0.000001, 3, "0.000"}, {-0.0001, 2, "-0.00"}, {123.456, 1, "123.5"},
        {1e21, 2, "1e+21"}, {9.995, 2, "9.99"}, {1.45, 1, "1.4"}, {0.5, 0, "1"}, {1e20, 2, "100000000000000000000.00"},
        {-1.005, 2, "-1.00"}, {123.456, 10, "123.4560000000"},
    };
    for (const auto& c : cases) LAPILLI_CHECK_EQ(js_to_fixed(c.v, c.d), std::string(c.text));
}

LAPILLI_TEST("Number(text) equals JavaScript") {
    LAPILLI_CHECK_EQ(js_to_number(" 12 "), 12.0);
    LAPILLI_CHECK_EQ(js_to_number("0x1f"), 31.0);
    LAPILLI_CHECK_EQ(js_to_number("0b101"), 5.0);
    LAPILLI_CHECK_EQ(js_to_number("0o17"), 15.0);
    LAPILLI_CHECK_EQ(js_to_number(" -1.5e3 "), -1500.0);
    LAPILLI_CHECK_EQ(js_to_number("1e3"), 1000.0);
    LAPILLI_CHECK_EQ(js_to_number(".5"), 0.5);
    LAPILLI_CHECK_EQ(js_to_number("5."), 5.0);
    LAPILLI_CHECK_EQ(js_to_number(""), 0.0);
    for (const char* nan_text : {"abc", "+1_0", "1e", "Infinityx", "-0x10", "."}) LAPILLI_CHECK(std::isnan(js_to_number(nan_text)));
    LAPILLI_CHECK(std::isinf(js_to_number("-Infinity")));
    LAPILLI_CHECK(std::isinf(js_to_number("1e400")));
}

LAPILLI_TEST("Math.round, min, max and the integer tests follow JavaScript") {
    LAPILLI_CHECK_EQ(js_round(-2.5), -2.0);
    LAPILLI_CHECK_EQ(js_round(2.5), 3.0);
    LAPILLI_CHECK(std::signbit(js_round(-0.4)));
    LAPILLI_CHECK(std::isnan(js_min({1.0, std::nan(""), 3.0})));
    LAPILLI_CHECK_EQ(js_max({1.0, 3.0, 2.0}), 3.0);
    LAPILLI_CHECK(std::isinf(js_min(std::vector<double>{})));
    LAPILLI_CHECK(std::signbit(js_min(0.0, -0.0)));
    LAPILLI_CHECK(!std::signbit(js_max(-0.0, 0.0)));
    LAPILLI_CHECK(js_is_integer(5.0) && !js_is_integer(5.5) && !js_is_integer(INFINITY));
    LAPILLI_CHECK(js_is_safe_integer(9007199254740991.0) && !js_is_safe_integer(9007199254740992.0));
}
