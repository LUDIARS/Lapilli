// Text helpers over UTF-8 std::string with the JavaScript semantics the Node services rely on.
// JavaScript strings are UTF-16: length, comparison (Array#sort, `<`) and slice work on UTF-16
// code units. Invalid UTF-8 is decoded as the WHATWG decoder (TextDecoder, Buffer#toString)
// does: each maximal invalid subpart becomes one U+FFFD.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::text {

/** True when `text` is well-formed UTF-8 (no overlongs, surrogates or code points > U+10FFFF). */
bool is_valid_utf8(std::string_view text) noexcept;
/** `text` with every invalid subpart replaced by U+FFFD (unchanged when already valid). */
std::string sanitize_utf8(std::string_view text);

/** UTF-8 -> UTF-16 code units (invalid input as sanitize_utf8). */
std::u16string to_utf16(std::string_view utf8);
/** UTF-16 -> UTF-8 (lone surrogates become U+FFFD). */
std::string to_utf8(std::u16string_view utf16);

/** String#length. */
std::size_t js_length(std::string_view utf8);
/** -1 / 0 / 1 by UTF-16 code units (JavaScript's default string order). */
int js_compare(std::string_view a, std::string_view b);
/** Comparator for std::sort with JavaScript's default string order. */
struct JsLess {
    bool operator()(std::string_view a, std::string_view b) const { return js_compare(a, b) < 0; }
};
/** String#slice(start[, end]) on UTF-16 units (negative indices count from the end). */
std::string js_slice(std::string_view utf8, long long start, long long end);
std::string js_slice(std::string_view utf8, long long start);
/** String#trim (JavaScript's WhiteSpace + LineTerminator set). */
std::string js_trim(std::string_view utf8);
std::string js_trim_start(std::string_view utf8);
std::string js_trim_end(std::string_view utf8);
/** String#split(separator); an empty separator splits into UTF-16 units (as JavaScript does). */
std::vector<std::string> js_split(std::string_view text, std::string_view separator);
/** Array#join. */
std::string js_join(const std::vector<std::string>& items, std::string_view separator);

/** ASCII-only case mapping (other bytes unchanged). */
std::string ascii_lower(std::string_view text);
std::string ascii_upper(std::string_view text);
bool ascii_iequals(std::string_view a, std::string_view b) noexcept;
/** Split on one character, keeping empty fields ("a,,b" -> {"a","","b"}). */
std::vector<std::string> split(std::string_view text, char separator);
bool starts_with(std::string_view text, std::string_view prefix) noexcept;
bool ends_with(std::string_view text, std::string_view suffix) noexcept;

} // namespace lapilli::text
