#include "lapilli/text.hpp"

#include <algorithm>

namespace lapilli::text {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

/**
 * Decodes one code point at `i` (WHATWG UTF-8 decoder): returns the code point (or U+FFFD) and
 * advances `i` past it, or past the maximal invalid subpart.
 */
char32_t decode_one(std::string_view s, std::size_t& i) noexcept {
    const auto n = s.size();
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(s[at]); };
    const unsigned char c = byte(i);
    if (c < 0x80) {
        ++i;
        return c;
    }
    std::size_t needed = 0;
    char32_t cp = 0;
    unsigned char lower = 0x80;
    unsigned char upper = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        needed = 1;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
        if (c == 0xE0) lower = 0xA0;
        if (c == 0xED) upper = 0x9F;
        needed = 2;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {
        if (c == 0xF0) lower = 0x90;
        if (c == 0xF4) upper = 0x8F;
        needed = 3;
        cp = c & 0x07u;
    } else {
        ++i;
        return kReplacement;
    }
    std::size_t at = i + 1;
    for (std::size_t k = 0; k < needed; ++k, ++at) {
        if (at >= n) {
            i = at;
            return kReplacement;
        }
        const unsigned char next = byte(at);
        if (next < lower || next > upper) {
            i = at; // the offending byte starts the next sequence
            return kReplacement;
        }
        lower = 0x80;
        upper = 0xBF;
        cp = (cp << 6) | (next & 0x3Fu);
    }
    i = at;
    return cp;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool is_js_space(char16_t c) {
    return c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

std::size_t clamp_index(long long index, std::size_t length) {
    const auto len = static_cast<long long>(length);
    if (index < 0) index = std::max(0LL, len + index);
    return static_cast<std::size_t>(std::min(index, len));
}

} // namespace

bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        const char32_t cp = decode_one(text, i);
        // A decoded U+FFFD is only valid when it was spelled out (EF BF BD).
        if (cp == kReplacement && !(i - start == 3 && text.substr(start, 3) == "\xEF\xBF\xBD")) return false;
    }
    return true;
}

std::string sanitize_utf8(std::string_view text) {
    if (is_valid_utf8(text)) return std::string(text);
    std::string out;
    out.reserve(text.size() + 8);
    std::size_t i = 0;
    while (i < text.size()) append_utf8(out, decode_one(text, i));
    return out;
}

std::u16string to_utf16(std::string_view utf8) {
    std::u16string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = decode_one(utf8, i);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string to_utf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size());
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        char32_t cp = utf16[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < utf16.size() && utf16[i + 1] >= 0xDC00 && utf16[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(utf16[i + 1]) - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = kReplacement;
        }
        append_utf8(out, cp);
    }
    return out;
}

std::size_t js_length(std::string_view utf8) {
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < utf8.size()) units += decode_one(utf8, i) >= 0x10000 ? 2 : 1;
    return units;
}

int js_compare(std::string_view a, std::string_view b) {
    // Byte order equals code point order; it differs from UTF-16 order only when a supplementary
    // character meets U+E000..U+FFFF (lead bytes >= 0xEE), or when the input is invalid.
    bool simple = true;
    for (unsigned char c : a)
        if (c >= 0xEE) {
            simple = false;
            break;
        }
    if (simple)
        for (unsigned char c : b)
            if (c >= 0xEE) {
                simple = false;
                break;
            }
    if (simple && is_valid_utf8(a) && is_valid_utf8(b)) {
        const int r = a.compare(b);
        return r < 0 ? -1 : (r > 0 ? 1 : 0);
    }
    const int r = to_utf16(a).compare(to_utf16(b));
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

std::string js_slice(std::string_view utf8, long long start, long long end) {
    const auto units = to_utf16(utf8);
    const auto from = clamp_index(start, units.size());
    const auto to = clamp_index(end, units.size());
    if (to <= from) return {};
    return to_utf8(std::u16string_view(units).substr(from, to - from));
}

std::string js_slice(std::string_view utf8, long long start) {
    const auto units = to_utf16(utf8);
    const auto from = clamp_index(start, units.size());
    return to_utf8(std::u16string_view(units).substr(from));
}

std::string js_trim(std::string_view utf8) {
    const auto units = to_utf16(utf8);
    std::size_t from = 0;
    std::size_t to = units.size();
    while (from < to && is_js_space(units[from])) ++from;
    while (to > from && is_js_space(units[to - 1])) --to;
    return to_utf8(std::u16string_view(units).substr(from, to - from));
}

std::string js_trim_start(std::string_view utf8) {
    const auto units = to_utf16(utf8);
    std::size_t from = 0;
    while (from < units.size() && is_js_space(units[from])) ++from;
    return to_utf8(std::u16string_view(units).substr(from));
}

std::string js_trim_end(std::string_view utf8) {
    const auto units = to_utf16(utf8);
    std::size_t to = units.size();
    while (to > 0 && is_js_space(units[to - 1])) --to;
    return to_utf8(std::u16string_view(units).substr(0, to));
}

std::vector<std::string> js_split(std::string_view text, std::string_view separator) {
    std::vector<std::string> out;
    if (separator.empty()) {
        for (char16_t unit : to_utf16(text)) out.push_back(to_utf8(std::u16string(1, unit)));
        return out;
    }
    std::size_t from = 0;
    for (;;) {
        const auto at = text.find(separator, from);
        if (at == std::string_view::npos) {
            out.emplace_back(text.substr(from));
            return out;
        }
        out.emplace_back(text.substr(from, at - from));
        from = at + separator.size();
    }
}

std::string js_join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += separator;
        out += items[i];
    }
    return out;
}

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string ascii_upper(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto at = text.find(separator, from);
        if (at == std::string_view::npos) {
            out.emplace_back(text.substr(from));
            return out;
        }
        out.emplace_back(text.substr(from, at - from));
        from = at + 1;
    }
}

bool starts_with(std::string_view text, std::string_view prefix) noexcept { return text.substr(0, prefix.size()) == prefix; }
bool ends_with(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

} // namespace lapilli::text
