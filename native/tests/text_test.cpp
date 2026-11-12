// lapilli::text: UTF-8 validation / sanitizing as the WHATWG decoder (Buffer#toString) does it, and
// the UTF-16 string semantics of JavaScript. Values recorded from Node v22.
#include "lapilli/test/check.hpp"
#include "lapilli/text.hpp"

using namespace lapilli::text;

namespace {
std::string hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 0xf];
    }
    return out;
}
} // namespace

LAPILLI_TEST("invalid UTF-8 becomes U+FFFD per maximal subpart, as Buffer#toString") {
    // Node: Buffer.from([...]).toString() as hex
    const std::pair<std::string, const char*> cases[] = {
        {"a\xE3\x81" "A", "61efbfbd41"}, {"\xE3\x81", "efbfbd"}, {"\xFF\xFE", "efbfbdefbfbd"},
        {"\xED\xA0\x80", "efbfbdefbfbdefbfbd"}, {"\xF0\x90\x80", "efbfbd"}, {"\xC0\xAF", "efbfbdefbfbd"},
        {"\xF4\x90\x80\x80", "efbfbdefbfbdefbfbdefbfbd"}, {"\xE0\x80\x80", "efbfbdefbfbdefbfbd"},
    };
    for (const auto& [bytes, expected] : cases) {
        LAPILLI_CHECK(!is_valid_utf8(bytes));
        LAPILLI_CHECK_EQ(hex(sanitize_utf8(bytes)), std::string(expected));
    }
    LAPILLI_CHECK(is_valid_utf8("plain \xC3\xA9 \xF0\x9F\x98\x80 \xEF\xBF\xBD"));
    LAPILLI_CHECK_EQ(sanitize_utf8("ok"), std::string("ok"));
    LAPILLI_CHECK(is_valid_utf8(""));
}

LAPILLI_TEST("length, slice, compare work on UTF-16 code units") {
    LAPILLI_CHECK_EQ(js_length("\xF0\x9F\x98\x80" "a"), 3u); // "\u{1F600}a".length
    LAPILLI_CHECK_EQ(js_length("\xC3\xA9"), 1u);
    LAPILLI_CHECK_EQ(js_slice("abcdef", -3), std::string("def"));
    LAPILLI_CHECK_EQ(js_slice("abcdef", 2, -1), std::string("cde"));
    LAPILLI_CHECK_EQ(js_slice("abc", 5), std::string(""));
    // "a😀b".slice(1, 2) is a lone surrogate in JavaScript; UTF-8 cannot carry it: U+FFFD.
    LAPILLI_CHECK_EQ(js_slice("a\xF0\x9F\x98\x80" "b", 1, 2), std::string("\xEF\xBF\xBD"));
    // U+FF21 sorts after U+1D11E by bytes but before it in UTF-16.
    LAPILLI_CHECK(js_compare("\xEF\xBC\xA1", "\xF0\x9D\x84\x9E") > 0);
    LAPILLI_CHECK(js_compare("a", "b") < 0);
    LAPILLI_CHECK_EQ(js_compare("x", "x"), 0);
    LAPILLI_CHECK(JsLess{}("A", "a"));
    LAPILLI_CHECK_EQ(to_utf8(to_utf16("h\xC3\xA9\xF0\x9F\x98\x80")), std::string("h\xC3\xA9\xF0\x9F\x98\x80"));
}

LAPILLI_TEST("trim, split, join and the ASCII helpers follow JavaScript") {
    LAPILLI_CHECK_EQ(js_trim("\xE3\x80\x80\xEF\xBB\xBF x \xE2\x80\xA8"), std::string("x")); // "\u3000\ufeff x \u2028".trim()
    LAPILLI_CHECK_EQ(js_trim_start(" x "), std::string("x "));
    LAPILLI_CHECK_EQ(js_trim_end(" x "), std::string(" x"));
    LAPILLI_CHECK(js_split("a,,b,", ",") == (std::vector<std::string>{"a", "", "b", ""}));
    LAPILLI_CHECK_EQ(js_split("a\xF0\x9F\x98\x80", "").size(), 3u); // "a😀".split("") -> 3 units
    LAPILLI_CHECK_EQ(js_join({"a", "b", "c"}, "-"), std::string("a-b-c"));
    LAPILLI_CHECK(split("C:\\a;;D:\\b", ';') == (std::vector<std::string>{"C:\\a", "", "D:\\b"}));
    LAPILLI_CHECK_EQ(ascii_lower("AbC\xC3\x89"), std::string("abc\xC3\x89"));
    LAPILLI_CHECK_EQ(ascii_upper("abc"), std::string("ABC"));
    LAPILLI_CHECK(ascii_iequals("PATH", "Path"));
    LAPILLI_CHECK(!ascii_iequals("PATH", "PAT"));
    LAPILLI_CHECK(starts_with("abc", "ab") && !starts_with("a", "ab"));
    LAPILLI_CHECK(ends_with("abc", "bc") && !ends_with("c", "bc"));
}

LAPILLI_TEST("huge input: a large invalid buffer is sanitized in linear time") {
    std::string big(4u << 20, 'a');
    for (std::size_t i = 0; i < big.size(); i += 1000) big[i] = '\xFF';
    const auto clean = sanitize_utf8(big);
    LAPILLI_CHECK(is_valid_utf8(clean));
    LAPILLI_CHECK_EQ(clean.size(), big.size() + 2 * ((big.size() + 999) / 1000));
}
