// lapilli::json: JSON.parse / JSON.stringify semantics (values recorded from Node v22) and the
// Lorica budget on parsing: at the limit, one over, broken and huge / deeply nested input.
#include "lapilli/json.hpp"
#include "lapilli/test/check.hpp"

#include <cmath>
#include <limits>

using lapilli::Json;
using namespace lapilli::json;

namespace {
JsonErrorKind kind_of(std::string_view text, const JsonLimits& limits = {}) {
    JsonErrorKind kind = JsonErrorKind::invalid;
    std::string why;
    LAPILLI_CHECK(!try_parse(text, limits, &why, &kind).has_value());
    LAPILLI_CHECK(!why.empty());
    return kind;
}
} // namespace

LAPILLI_TEST("numbers print as Number#toString (String(n))") {
    const std::pair<double, const char*> cases[] = {
        {0.1 + 0.2, "0.30000000000000004"}, {1e21, "1e+21"}, {123456789012.0, "123456789012"}, {1e-7, "1e-7"},
        {0.000001, "0.000001"}, {-1.5e-7, "-1.5e-7"}, {5e-324, "5e-324"}, {1.7976931348623157e308, "1.7976931348623157e+308"},
        {100.0, "100"}, {9007199254740994.0, "9007199254740994"}, {0.5, "0.5"}, {123.456, "123.456"}, {-0.0, "0"}, {-12.0, "-12"},
        {123e-20, "1.23e-18"}, {1.0 / 3.0, "0.3333333333333333"}, {4.35, "4.35"}, {0.000001234, "0.000001234"},
    };
    for (const auto& [value, text] : cases) LAPILLI_CHECK_EQ(number_to_js_string(value), std::string(text));
    LAPILLI_CHECK_EQ(number_to_js_string(std::numeric_limits<double>::quiet_NaN()), std::string("NaN"));
    LAPILLI_CHECK_EQ(number_to_js_string(std::numeric_limits<double>::infinity()), std::string("Infinity"));
    LAPILLI_CHECK_EQ(number_to_js_string(-std::numeric_limits<double>::infinity()), std::string("-Infinity"));
    LAPILLI_CHECK_EQ(number_to_json(std::numeric_limits<double>::infinity()), std::string("null"));
    LAPILLI_CHECK_EQ(number_to_json(-0.0), std::string("0"));
}

LAPILLI_TEST("stringify keeps key order and matches JSON.stringify (indent 0 and 2)") {
    Json value = parse(R"({"b":[1,{}],"a":[],"c":"x\u0001\"é","d":1.0,"e":-0,"f":2.50})");
    LAPILLI_CHECK_EQ(stringify(value), std::string("{\"b\":[1,{}],\"a\":[],\"c\":\"x\\u0001\\\"\xC3\xA9\",\"d\":1,\"e\":0,\"f\":2.5}"));
    Json pretty = parse(R"({"a":[1,{}],"b":[],"c":"x"})");
    LAPILLI_CHECK_EQ(stringify(pretty, 2), std::string("{\n  \"a\": [\n    1,\n    {}\n  ],\n  \"b\": [],\n  \"c\": \"x\"\n}"));
    // Node: JSON.stringify({b:1,a:[1.5,"\u0000",null,true],c:{}}) / JSON.stringify([NaN,Infinity,-0])
    Json built = Json::object();
    built["b"] = 1;
    built["a"] = Json::array({1.5, std::string("\0", 1), nullptr, true});
    built["c"] = Json::object();
    LAPILLI_CHECK_EQ(stringify(built), std::string(R"({"b":1,"a":[1.5,"\u0000",null,true],"c":{}})"));
    Json non_finite = Json::array({std::nan(""), std::numeric_limits<double>::infinity(), -0.0});
    LAPILLI_CHECK_EQ(stringify(non_finite), std::string("[null,null,0]"));
    LAPILLI_CHECK_EQ(stringify(Json(1), 50), std::string("1"));
}

LAPILLI_TEST("integers beyond 2^53 read and print as JSON.parse makes them") {
    // Node: JSON.stringify(JSON.parse("[9007199254740993, 18446744073709551616, -9007199254740993, 1.0, 1e2]"))
    Json value = parse("[9007199254740993, 18446744073709551616, -9007199254740993, 1.0, 1e2]");
    LAPILLI_CHECK_EQ(stringify(value), std::string("[9007199254740992,18446744073709552000,-9007199254740992,1,100]"));
    LAPILLI_CHECK_EQ(js_number_value(value[0]), 9007199254740992.0);
    LAPILLI_CHECK(std::isnan(js_number_value(Json("x"))));
    std::string out;
    append_json_string(out, "a\n\"");
    append_json_number(out, 0.1);
    LAPILLI_CHECK_EQ(out, std::string("\"a\\n\\\"\"0.1"));
}

LAPILLI_TEST("boundary: bytes at the limit parse, one byte over is too_large") {
    JsonLimits limits{8, 100, 8};
    LAPILLI_CHECK_EQ(parse("[1,2,34]", limits).size(), 3u);
    LAPILLI_CHECK(kind_of("[1,2,345]", limits) == JsonErrorKind::too_large);
    try {
        parse("[1,2,345]", limits);
        LAPILLI_CHECK(false);
    } catch (const JsonError& error) {
        LAPILLI_CHECK(error.kind() == JsonErrorKind::too_large);
        LAPILLI_CHECK_EQ(std::string(kind_name(error.kind())), std::string("too_large"));
    }
}

LAPILLI_TEST("boundary: nesting at the depth limit parses, one deeper is too_deep, huge nesting is refused early") {
    JsonLimits limits{1 << 20, 1 << 20, 4};
    LAPILLI_CHECK(parse("[[[[1]]]]", limits).is_array());
    LAPILLI_CHECK(kind_of("[[[[[1]]]]]", limits) == JsonErrorKind::too_deep);
    LAPILLI_CHECK(kind_of(std::string(100000, '['), JsonLimits{}) == JsonErrorKind::too_deep);
    LAPILLI_CHECK(kind_of(R"({"a":{"b":{"c":{"d":{"e":1}}}}})", limits) == JsonErrorKind::too_deep);
}

LAPILLI_TEST("boundary: values at the element limit parse, one more is too_large; huge input is refused") {
    JsonLimits limits{1 << 20, 4, 8};
    LAPILLI_CHECK(parse("[1,2,3]", limits).is_array()); // the array + 3 values
    LAPILLI_CHECK(kind_of("[1,2,3,4]", limits) == JsonErrorKind::too_large);
    LAPILLI_CHECK(parse(R"({"a":1})", JsonLimits{100, 3, 8}).is_object()); // object + key + value
    LAPILLI_CHECK(kind_of(R"({"a":1,"b":2})", JsonLimits{100, 3, 8}) == JsonErrorKind::too_large);
    const std::string huge = "[" + std::string(1u << 20, '1') + "]";
    LAPILLI_CHECK(kind_of(huge, JsonLimits{1u << 20, 10, 8}) == JsonErrorKind::too_large);
}

LAPILLI_TEST("broken JSON is invalid (also bad UTF-8, empty text, trailing garbage and double overflow)") {
    for (const char* text : {"{\"a\":", "\"\xFF\"", "", "[1,]", "{a:1}", "1 2", "[1e400]", "nul"})
        LAPILLI_CHECK(kind_of(text) == JsonErrorKind::invalid);
    LAPILLI_CHECK_THROWS_AS(parse("{"), JsonError);
}

LAPILLI_TEST("an existing budget is shared by several documents") {
    lorica::input_budget budget(JsonLimits{12, 100, 8}.budget());
    LAPILLI_CHECK(parse("[1,2]", budget).is_array());
    LAPILLI_CHECK(parse("[3,4]", budget).is_array());
    LAPILLI_CHECK_THROWS_AS(parse("[5,6]", budget), JsonError);
}
