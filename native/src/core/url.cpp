#include "lapilli/url.hpp"

#include "lapilli/text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <vector>

namespace lapilli::url {
namespace {

bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }


void percent_encode_into(std::string& out, std::string_view text, bool (*encode)(unsigned char)) {
    static const char* const hex = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if (c < 0x21 || c >= 0x7F || encode(c)) {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
}

bool path_set(unsigned char c) { return c == '"' || c == '#' || c == '<' || c == '>' || c == '?' || c == '`' || c == '{' || c == '}'; }
bool query_set(unsigned char c) { return c == '"' || c == '#' || c == '<' || c == '>' || c == '\''; }
bool fragment_set(unsigned char c) { return c == '"' || c == '<' || c == '>' || c == '`'; }
bool userinfo_set(unsigned char c) {
    return path_set(c) || c == '/' || c == ':' || c == ';' || c == '=' || c == '@' || (c >= '[' && c <= '^') || c == '|';
}

std::optional<std::uint64_t> parse_ipv4_number(std::string_view part) {
    if (part.empty()) return std::nullopt;
    int radix = 10;
    if (part.size() >= 2 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X')) { radix = 16; part.remove_prefix(2); }
    else if (part.size() >= 2 && part[0] == '0') { radix = 8; part.remove_prefix(1); }
    if (part.empty()) return 0;
    std::uint64_t value = 0;
    for (char c : part) {
        int digit = 0;
        if (is_digit(c)) digit = c - '0';
        else if (radix == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) digit = lower(c) - 'a' + 10;
        else return std::nullopt;
        if (digit >= radix) return std::nullopt;
        value = value * static_cast<std::uint64_t>(radix) + static_cast<std::uint64_t>(digit);
        if (value > 0xFFFFFFFFFFULL) return std::nullopt;
    }
    return value;
}

bool ends_in_number(std::string_view host) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') { parts.push_back(host.substr(start, i - start)); start = i + 1; }
    }
    if (parts.back().empty()) {
        if (parts.size() == 1) return false;
        parts.pop_back();
    }
    const auto last = parts.back();
    if (!last.empty() && std::all_of(last.begin(), last.end(), is_digit)) return true;
    return parse_ipv4_number(last).has_value() && last.size() >= 2 && last[0] == '0' && (last[1] == 'x' || last[1] == 'X');
}

} // namespace

std::optional<std::string> parse_ipv4_host(std::string_view host) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') { parts.push_back(host.substr(start, i - start)); start = i + 1; }
    }
    if (parts.back().empty() && parts.size() > 1) parts.pop_back();
    if (parts.size() > 4) return std::nullopt;
    std::vector<std::uint64_t> numbers;
    for (auto part : parts) {
        auto n = parse_ipv4_number(part);
        if (!n) return std::nullopt;
        numbers.push_back(*n);
    }
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) if (numbers[i] > 255) return std::nullopt;
    std::uint64_t limit = 1;
    for (std::size_t i = 0; i < 5 - numbers.size(); ++i) limit *= 256;
    if (numbers.back() >= limit) return std::nullopt;
    std::uint64_t ipv4 = numbers.back();
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) ipv4 += numbers[i] << (8 * (3 - i));
    return std::to_string((ipv4 >> 24) & 255) + "." + std::to_string((ipv4 >> 16) & 255) + "." + std::to_string((ipv4 >> 8) & 255)
        + "." + std::to_string(ipv4 & 255);
}

std::optional<std::string> parse_ipv6_host(std::string_view input) {
    std::array<std::uint16_t, 8> address{};
    int piece = 0;
    int compress = -1;
    std::size_t p = 0;
    auto at = [&](std::size_t i) -> char { return i < input.size() ? input[i] : '\0'; };
    if (at(p) == ':') {
        if (at(p + 1) != ':') return std::nullopt;
        p += 2;
        compress = ++piece;
    }
    while (p < input.size()) {
        if (piece == 8) return std::nullopt;
        if (at(p) == ':') {
            if (compress != -1) return std::nullopt;
            ++p;
            compress = ++piece;
            continue;
        }
        unsigned value = 0;
        int length = 0;
        while (length < 4 && std::isxdigit(static_cast<unsigned char>(at(p)))) {
            const char c = lower(at(p));
            value = value * 16 + static_cast<unsigned>(is_digit(c) ? c - '0' : c - 'a' + 10);
            ++p;
            ++length;
        }
        if (at(p) == '.') {
            if (length == 0) return std::nullopt;
            p -= static_cast<std::size_t>(length);
            if (piece > 6) return std::nullopt;
            int numbers_seen = 0;
            while (p < input.size()) {
                int ipv4_piece = -1;
                if (numbers_seen > 0) {
                    if (at(p) == '.' && numbers_seen < 4) ++p;
                    else return std::nullopt;
                }
                if (!is_digit(at(p))) return std::nullopt;
                while (is_digit(at(p))) {
                    const int number = at(p) - '0';
                    if (ipv4_piece == -1) ipv4_piece = number;
                    else if (ipv4_piece == 0) return std::nullopt;
                    else ipv4_piece = ipv4_piece * 10 + number;
                    if (ipv4_piece > 255) return std::nullopt;
                    ++p;
                }
                address[static_cast<std::size_t>(piece)] = static_cast<std::uint16_t>(address[static_cast<std::size_t>(piece)] * 0x100 + ipv4_piece);
                ++numbers_seen;
                if (numbers_seen == 2 || numbers_seen == 4) ++piece;
            }
            if (numbers_seen != 4) return std::nullopt;
            break;
        }
        if (at(p) == ':') {
            ++p;
            if (p >= input.size()) return std::nullopt;
        } else if (p < input.size()) {
            return std::nullopt;
        }
        address[static_cast<std::size_t>(piece)] = static_cast<std::uint16_t>(value);
        ++piece;
    }
    if (compress != -1) {
        int swaps = piece - compress;
        piece = 7;
        while (piece != 0 && swaps > 0) {
            std::swap(address[static_cast<std::size_t>(piece)], address[static_cast<std::size_t>(compress + swaps - 1)]);
            --piece;
            --swaps;
        }
    } else if (piece != 8) {
        return std::nullopt;
    }
    // Serialize: compress the first longest run of two or more zero pieces.
    int best_start = -1, best_len = 0;
    for (int i = 0; i < 8;) {
        if (address[static_cast<std::size_t>(i)] != 0) { ++i; continue; }
        int j = i;
        while (j < 8 && address[static_cast<std::size_t>(j)] == 0) ++j;
        if (j - i > best_len && j - i >= 2) { best_start = i; best_len = j - i; }
        i = j;
    }
    std::string out = "[";
    bool ignore0 = false;
    static const char* const hex = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
        if (ignore0 && address[static_cast<std::size_t>(i)] == 0) continue;
        ignore0 = false;
        if (i == best_start) {
            out += i == 0 ? "::" : ":";
            ignore0 = true;
            continue;
        }
        std::string piece_text;
        unsigned v = address[static_cast<std::size_t>(i)];
        do { piece_text.insert(piece_text.begin(), hex[v & 0xF]); v >>= 4; } while (v);
        out += piece_text;
        if (i != 7) out.push_back(':');
    }
    return out + "]";
}

namespace {

std::optional<std::string> parse_host(std::string_view raw, bool special) {
    if (!raw.empty() && raw.front() == '[') {
        if (raw.back() != ']') return std::nullopt;
        return parse_ipv6_host(raw.substr(1, raw.size() - 2));
    }
    if (!special) return std::string(raw);
    const std::string decoded = percent_decode(raw);
    std::string ascii;
    for (char c : decoded) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80) return std::nullopt; // IDNA is not supported: reject instead of guessing.
        if (u <= 0x20 || u == 0x7F || c == '#' || c == '%' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' || c == '@'
            || c == '[' || c == '\\' || c == ']' || c == '^' || c == '|') return std::nullopt;
        ascii.push_back(lower(c));
    }
    if (ascii.empty()) return std::nullopt;
    if (ends_in_number(ascii)) return parse_ipv4_host(ascii);
    return ascii;
}

bool single_dot(std::string_view s) {
    std::string l;
    for (char c : s) l.push_back(lower(c));
    return l == "." || l == "%2e";
}
bool double_dot(std::string_view s) {
    std::string l;
    for (char c : s) l.push_back(lower(c));
    return l == ".." || l == ".%2e" || l == "%2e." || l == "%2e%2e";
}

std::string normalize_path(std::string_view raw, bool special) {
    std::string text(raw);
    if (special) std::replace(text.begin(), text.end(), '\\', '/');
    std::vector<std::string> segments;
    std::size_t start = text.empty() || text[0] != '/' ? 0 : 1;
    if (text.empty()) return "/";
    for (std::size_t i = start; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '/') {
            const std::string_view segment(text.data() + start, i - start);
            const bool last = i == text.size();
            if (double_dot(segment)) {
                if (!segments.empty()) segments.pop_back();
                if (last) segments.emplace_back();
            } else if (single_dot(segment)) {
                if (last) segments.emplace_back();
            } else {
                std::string encoded;
                percent_encode_into(encoded, segment, path_set);
                segments.push_back(encoded);
            }
            start = i + 1;
        }
    }
    std::string out;
    for (const auto& segment : segments) out += "/" + segment;
    return out.empty() ? "/" : out;
}

} // namespace

std::string percent_decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()
            && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            auto v = [](char c) { return is_digit(c) ? c - '0' : lower(c) - 'a' + 10; };
            out.push_back(static_cast<char>(v(text[i + 1]) * 16 + v(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool is_special_scheme(std::string_view protocol) noexcept {
    return protocol == "http:" || protocol == "https:" || protocol == "ws:" || protocol == "wss:" || protocol == "ftp:"
        || protocol == "file:";
}

std::string Url::origin() const {
    if (!special || protocol == "file:") return "null";
    return protocol + "//" + host();
}

std::string Url::href() const {
    if (!special) return protocol + opaque;
    std::string out = protocol + "//";
    if (!username.empty() || !password.empty()) {
        out += username;
        if (!password.empty()) out += ":" + password;
        out += "@";
    }
    return out + host() + pathname + search + hash;
}

std::string Url::bare_hostname() const {
    if (hostname.size() >= 2 && hostname.front() == '[' && hostname.back() == ']') return hostname.substr(1, hostname.size() - 2);
    return hostname;
}

std::uint16_t Url::effective_port() const {
    const std::string value = port.empty() ? default_port(protocol) : port;
    return value.empty() ? std::uint16_t{0} : static_cast<std::uint16_t>(std::stoul(value));
}

std::optional<Url> parse(std::string_view input) {
    std::string text;
    {
        std::size_t begin = 0, end = input.size();
        while (begin < end && static_cast<unsigned char>(input[begin]) <= 0x20) ++begin;
        while (end > begin && static_cast<unsigned char>(input[end - 1]) <= 0x20) --end;
        for (std::size_t i = begin; i < end; ++i) {
            if (input[i] != '\t' && input[i] != '\n' && input[i] != '\r') text.push_back(input[i]);
        }
    }
    if (text.empty() || !is_alpha(text[0])) return std::nullopt;
    std::size_t colon = 0;
    while (colon < text.size() && (is_alpha(text[colon]) || is_digit(text[colon]) || text[colon] == '+' || text[colon] == '-'
               || text[colon] == '.')) ++colon;
    if (colon >= text.size() || text[colon] != ':') return std::nullopt;
    Url url;
    for (std::size_t i = 0; i <= colon; ++i) url.protocol.push_back(lower(text[i]));
    url.special = is_special_scheme(url.protocol);
    std::string_view rest(text);
    rest.remove_prefix(colon + 1);
    if (!url.special) {
        url.opaque = std::string(rest);
        return url;
    }
    if (url.protocol == "file:") {
        url.pathname = normalize_path(rest, true);
        return url;
    }
    while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) rest.remove_prefix(1);
    const std::size_t authority_end = rest.find_first_of("/\\?#");
    std::string_view authority = rest.substr(0, authority_end);
    rest.remove_prefix(authority_end == std::string_view::npos ? rest.size() : authority_end);
    const std::size_t at = authority.rfind('@');
    if (at != std::string_view::npos) {
        const std::string_view userinfo = authority.substr(0, at);
        const std::size_t sep = userinfo.find(':');
        percent_encode_into(url.username, userinfo.substr(0, sep), userinfo_set);
        if (sep != std::string_view::npos) percent_encode_into(url.password, userinfo.substr(sep + 1), userinfo_set);
        authority.remove_prefix(at + 1);
    }
    std::string_view host_text = authority;
    std::string_view port_text;
    bool has_port = false;
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host_text = authority.substr(0, close + 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return std::nullopt;
            port_text = authority.substr(close + 2);
            has_port = true;
        }
    } else {
        const std::size_t sep = authority.find(':');
        if (sep != std::string_view::npos) {
            host_text = authority.substr(0, sep);
            port_text = authority.substr(sep + 1);
            has_port = true;
        }
    }
    auto host = parse_host(host_text, true);
    if (!host) return std::nullopt;
    url.hostname = *host;
    if (has_port && !port_text.empty()) {
        if (!std::all_of(port_text.begin(), port_text.end(), is_digit) || port_text.size() > 5) return std::nullopt;
        const unsigned long value = std::stoul(std::string(port_text));
        if (value > 65535) return std::nullopt;
        const std::string normalized = std::to_string(value);
        if (normalized != default_port(url.protocol)) url.port = normalized;
    }
    const std::size_t hash_pos = rest.find('#');
    const std::string_view before_hash = rest.substr(0, hash_pos);
    const std::size_t query_pos = before_hash.find('?');
    url.pathname = normalize_path(before_hash.substr(0, query_pos), true);
    if (query_pos != std::string_view::npos) {
        url.search = "?";
        percent_encode_into(url.search, before_hash.substr(query_pos + 1), query_set);
        if (url.search == "?") url.search.clear();
    }
    if (hash_pos != std::string_view::npos) {
        url.hash = "#";
        percent_encode_into(url.hash, rest.substr(hash_pos + 1), fragment_set);
        if (url.hash == "#") url.hash.clear();
    }
    return url;
}

std::string default_port(std::string_view protocol) {
    if (protocol == "http:" || protocol == "ws:") return "80";
    if (protocol == "https:" || protocol == "wss:") return "443";
    if (protocol == "ftp:") return "21";
    return "";
}

std::optional<Url> Url::resolve(std::string_view reference) const { return url::resolve(reference, *this); }

std::optional<Url> resolve(std::string_view raw_reference, const Url& base) {
    if (auto absolute = parse(raw_reference)) return absolute;
    // Trim C0 controls / spaces and drop tab / newline, as the parser does.
    std::string reference;
    {
        std::size_t begin = 0, end = raw_reference.size();
        while (begin < end && static_cast<unsigned char>(raw_reference[begin]) <= 0x20) ++begin;
        while (end > begin && static_cast<unsigned char>(raw_reference[end - 1]) <= 0x20) --end;
        for (std::size_t i = begin; i < end; ++i)
            if (raw_reference[i] != '\t' && raw_reference[i] != '\n' && raw_reference[i] != '\r') reference.push_back(raw_reference[i]);
    }
    if (!base.special) return std::nullopt; // a relative reference needs a hierarchical base
    if (base.special) std::replace(reference.begin(), reference.end(), '\\', '/');
    const std::string without_hash = [&] {
        Url copy = base;
        copy.hash.clear();
        return copy.href();
    }();
    if (reference.empty()) return parse(without_hash);
    if (reference.front() == '#') return parse(without_hash + reference);
    if (base.protocol == "file:") {
        if (reference.front() == '/') return parse("file://" + reference);
    }
    std::string authority = base.protocol + "//";
    if (!base.username.empty() || !base.password.empty()) {
        authority += base.username;
        if (!base.password.empty()) authority += ":" + base.password;
        authority += "@";
    }
    authority += base.host();
    if (reference.front() == '?') return parse(authority + base.pathname + reference);
    if (reference.size() > 1 && reference[0] == '/' && reference[1] == '/') return parse(base.protocol + reference);
    if (reference.front() == '/') return parse(authority + reference);
    // A relative path: merge with the base path's directory; the parser removes dot segments.
    const auto slash = base.pathname.find_last_of('/');
    const std::string directory = slash == std::string::npos ? std::string("/") : base.pathname.substr(0, slash + 1);
    return parse(authority + directory + reference);
}

std::string encode_uri_component(std::string_view raw) {
    static constexpr const char* hex = "0123456789ABCDEF";
    const std::string text = lapilli::text::sanitize_utf8(raw);
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        const bool keep = is_alpha(static_cast<char>(c)) || is_digit(static_cast<char>(c)) || c == '-' || c == '_' || c == '.' || c == '!' ||
                          c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
        if (keep) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

namespace {
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::optional<std::string> decode_uri_component(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size()) return std::nullopt;
        const int hi = hex_value(text[i + 1]);
        const int lo = hex_value(text[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    if (!lapilli::text::is_valid_utf8(out)) return std::nullopt;
    return out;
}

QueryList parse_query(std::string_view query) {
    if (!query.empty() && query.front() == '?') query.remove_prefix(1);
    QueryList out;
    std::size_t start = 0;
    while (start <= query.size()) {
        auto end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();
        const std::string_view pair = query.substr(start, end - start);
        if (!pair.empty()) {
            const auto eq = pair.find('=');
            std::string name(pair.substr(0, eq));
            std::string value(eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1));
            std::replace(name.begin(), name.end(), '+', ' ');
            std::replace(value.begin(), value.end(), '+', ' ');
            out.emplace_back(lapilli::text::sanitize_utf8(percent_decode(name)), lapilli::text::sanitize_utf8(percent_decode(value)));
        }
        start = end + 1;
    }
    return out;
}

namespace {
void form_encode(std::string& out, std::string_view text) {
    static constexpr const char* hex = "0123456789ABCDEF";
    for (unsigned char c : lapilli::text::sanitize_utf8(text)) {
        if (is_alpha(static_cast<char>(c)) || is_digit(static_cast<char>(c)) || c == '*' || c == '-' || c == '.' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
}
} // namespace

std::string serialize_query(const QueryList& params) {
    std::string out;
    for (const auto& [name, value] : params) {
        if (!out.empty()) out.push_back('&');
        form_encode(out, name);
        out.push_back('=');
        form_encode(out, value);
    }
    return out;
}

std::optional<std::string> query_get(const QueryList& params, std::string_view name) {
    for (const auto& [key, value] : params)
        if (key == name) return value;
    return std::nullopt;
}

std::vector<std::string> query_get_all(const QueryList& params, std::string_view name) {
    std::vector<std::string> out;
    for (const auto& [key, value] : params)
        if (key == name) out.push_back(value);
    return out;
}

std::optional<std::uint32_t> parse_ipv4(std::string_view text) {
    std::size_t dots = 0;
    for (char c : text) dots += c == '.' ? 1u : 0u;
    if (dots != 3) return std::nullopt;
    std::uint32_t value = 0;
    std::size_t at = 0;
    for (int part_index = 0; part_index < 4; ++part_index) {
        std::size_t end = text.find('.', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view part = text.substr(at, end - at);
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part[0] == '0')) return std::nullopt;
        unsigned octet = 0;
        for (char c : part) {
            if (!is_digit(c)) return std::nullopt;
            octet = octet * 10 + static_cast<unsigned>(c - '0');
        }
        if (octet > 255) return std::nullopt;
        value = (value << 8) | octet;
        at = end + 1;
    }
    return value;
}

} // namespace lapilli::url
