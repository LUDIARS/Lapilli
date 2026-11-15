#include "lapilli/json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <system_error>

namespace lapilli::json {

namespace {

constexpr std::uint64_t kExact = 9007199254740992ULL; // 2^53

[[noreturn]] void fail(JsonErrorKind kind, const std::string& what) { throw JsonError(kind, what); }

} // namespace

const char* kind_name(JsonErrorKind kind) noexcept {
    switch (kind) {
    case JsonErrorKind::too_large: return "too_large";
    case JsonErrorKind::too_deep: return "too_deep";
    case JsonErrorKind::invalid: return "invalid";
    }
    return "invalid";
}

Json parse(std::string_view text, lorica::input_budget& budget) {
    if (!budget.consume(text.size(), 0, 1)) fail(JsonErrorKind::too_large, "JSON text exceeds its byte budget");
    Json::parser_callback_t charge = [&budget](int depth, Json::parse_event_t event, Json& /*parsed*/) {
        switch (event) {
        case Json::parse_event_t::object_start:
        case Json::parse_event_t::array_start:
            if (!budget.permits_depth(static_cast<std::size_t>(depth) + 1)) fail(JsonErrorKind::too_deep, "JSON nests deeper than its budget");
            if (!budget.consume(0, 1, 1)) fail(JsonErrorKind::too_large, "JSON has more values than its budget");
            break;
        case Json::parse_event_t::value:
        case Json::parse_event_t::key:
            if (!budget.consume(0, 1, 0)) fail(JsonErrorKind::too_large, "JSON has more values than its budget");
            break;
        default:
            break;
        }
        return true;
    };
    try {
        return Json::parse(text.begin(), text.end(), charge, /*allow_exceptions=*/true, /*ignore_comments=*/false);
    } catch (const nlohmann::json::exception& error) {
        fail(JsonErrorKind::invalid, error.what());
    }
}

Json parse(std::string_view text, const JsonLimits& limits) {
    lorica::input_budget budget(limits.budget());
    return parse(text, budget);
}

std::optional<Json> try_parse(std::string_view text, const JsonLimits& limits, std::string* why, JsonErrorKind* kind) {
    try {
        return parse(text, limits);
    } catch (const JsonError& error) {
        if (why != nullptr) *why = error.what();
        if (kind != nullptr) *kind = error.kind();
        return std::nullopt;
    }
}

std::string number_to_js_string(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    if (value == 0) return "0";
    if (std::fabs(value) < 9007199254740992.0 && std::trunc(value) == value) return std::to_string(static_cast<std::int64_t>(value));
    std::array<char, 64> buffer{};
    // Shortest round-trip digits in scientific form: d[.ddd]e[+-]x (ECMAScript Number::toString).
    auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::fabs(value), std::chars_format::scientific);
    if (error != std::errc()) return "NaN";
    std::string_view text(buffer.data(), static_cast<std::size_t>(end - buffer.data()));
    const auto e = text.find('e');
    std::string digits;
    for (char c : text.substr(0, e))
        if (c != '.') digits.push_back(c);
    int exponent = 0;
    {
        std::string_view exp_text = text.substr(e + 1);
        if (!exp_text.empty() && exp_text.front() == '+') exp_text.remove_prefix(1);
        std::from_chars(exp_text.data(), exp_text.data() + exp_text.size(), exponent);
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;
    std::string out = value < 0 ? "-" : "";
    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<std::size_t>(n));
        out += '.';
        out += digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-n), '0');
        out += digits;
    } else {
        out += digits.substr(0, 1);
        if (k > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += n - 1 >= 0 ? '+' : '-';
        out += std::to_string(std::abs(n - 1));
    }
    return out;
}

std::string number_to_json(double value) { return std::isfinite(value) ? number_to_js_string(value) : "null"; }

double js_number_value(const Json& value) {
    if (value.is_number_float()) return value.get<double>();
    if (value.is_number_unsigned()) return static_cast<double>(value.get<std::uint64_t>());
    if (value.is_number_integer()) return static_cast<double>(value.get<std::int64_t>());
    return std::numeric_limits<double>::quiet_NaN();
}

void append_json_string(std::string& out, std::string_view text) {
    static constexpr const char* hex = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xf];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

void append_json_number(std::string& out, double value) { out += number_to_json(value); }

namespace {

void write_number(std::string& out, const Json& value) {
    if (value.is_number_float()) {
        append_json_number(out, value.get<double>());
        return;
    }
    if (value.is_number_unsigned()) {
        const auto u = value.get<std::uint64_t>();
        out += u <= kExact ? std::to_string(u) : number_to_js_string(static_cast<double>(u));
        return;
    }
    const auto i = value.get<std::int64_t>();
    const auto magnitude = i < 0 ? static_cast<std::uint64_t>(-(i + 1)) + 1 : static_cast<std::uint64_t>(i);
    out += magnitude <= kExact ? std::to_string(i) : number_to_js_string(static_cast<double>(i));
}

void write(std::string& out, const Json& value, int indent, int level) {
    auto newline = [&](int depth) {
        out += '\n';
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
    };
    switch (value.type()) {
    case Json::value_t::null:
    case Json::value_t::discarded:
    case Json::value_t::binary:
        out += "null";
        break;
    case Json::value_t::boolean:
        out += value.get<bool>() ? "true" : "false";
        break;
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float:
        write_number(out, value);
        break;
    case Json::value_t::string:
        append_json_string(out, value.get_ref<const std::string&>());
        break;
    case Json::value_t::array: {
        if (value.empty()) {
            out += "[]";
            break;
        }
        out += '[';
        bool first = true;
        for (const auto& item : value) {
            if (!first) out += ',';
            first = false;
            if (indent > 0) newline(level + 1);
            write(out, item, indent, level + 1);
        }
        if (indent > 0) newline(level);
        out += ']';
        break;
    }
    case Json::value_t::object: {
        if (value.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        bool first = true;
        for (const auto& [key, item] : value.items()) {
            if (!first) out += ',';
            first = false;
            if (indent > 0) newline(level + 1);
            append_json_string(out, key);
            out += indent > 0 ? ": " : ":";
            write(out, item, indent, level + 1);
        }
        if (indent > 0) newline(level);
        out += '}';
        break;
    }
    }
}

} // namespace

std::string stringify(const Json& value, int indent) {
    std::string out;
    write(out, value, indent < 0 ? 0 : (indent > 10 ? 10 : indent), 0);
    return out;
}

} // namespace lapilli::json
