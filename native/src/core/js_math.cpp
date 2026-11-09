#include "lapilli/js_math.hpp"

#include "lapilli/json.hpp"
#include "lapilli/text.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace lapilli::js_math {

using json::number_to_js_string;
using text::js_trim;

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
} // namespace

double js_min(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return kNaN;
    if (a == 0 && b == 0) return std::signbit(a) ? a : b;
    return a < b ? a : b;
}

double js_max(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return kNaN;
    if (a == 0 && b == 0) return std::signbit(a) ? b : a;
    return a > b ? a : b;
}

double js_min(const std::vector<double>& values) {
    double out = kInf;
    for (double v : values) out = js_min(out, v);
    return out;
}
double js_min(std::initializer_list<double> values) { return js_min(std::vector<double>(values)); }

double js_max(const std::vector<double>& values) {
    double out = -kInf;
    for (double v : values) out = js_max(out, v);
    return out;
}
double js_max(std::initializer_list<double> values) { return js_max(std::vector<double>(values)); }

double js_round(double value) {
    if (!std::isfinite(value) || value == 0) return value;
    if (value > 0 && value < 0.5) return 0.0;
    if (value < 0 && value >= -0.5) return -0.0;
    double floor = std::floor(value);
    return value - floor >= 0.5 ? floor + 1 : floor;
}

double js_to_number(std::string_view raw) {
    std::string text = js_trim(raw);
    if (text.empty()) return 0;
    if (text == "Infinity" || text == "+Infinity") return kInf;
    if (text == "-Infinity") return -kInf;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X' || text[1] == 'o' || text[1] == 'O' || text[1] == 'b' || text[1] == 'B')) {
        int base = (text[1] == 'x' || text[1] == 'X') ? 16 : ((text[1] == 'o' || text[1] == 'O') ? 8 : 2);
        double value = 0;
        for (std::size_t i = 2; i < text.size(); ++i) {
            char c = text[i];
            int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99));
            if (digit >= base) return kNaN;
            value = value * base + digit;
        }
        return value;
    }
    // StrDecimalLiteral: [+-] (digits [. digits] | . digits) [e [+-] digits]
    std::size_t i = 0;
    bool negative = false;
    if (text[i] == '+' || text[i] == '-') negative = text[i++] == '-';
    std::size_t start = i;
    bool digits = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; digits = true; }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; digits = true; }
    }
    if (!digits) return kNaN;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        bool exponent = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; exponent = true; }
        if (!exponent) return kNaN;
    }
    if (i != text.size()) return kNaN;
    std::string body = text.substr(start);
    if (body[0] == '.') body = "0" + body;
    double value = 0;
    auto [end, error] = std::from_chars(body.data(), body.data() + body.size(), value);
    if (error == std::errc::result_out_of_range) value = body.find_first_of("eE") != std::string::npos && body.find("e-") != std::string::npos ? 0.0 : kInf;
    else if (error != std::errc() || end != body.data() + body.size()) return kNaN;
    return negative ? -value : value;
}

std::string js_to_fixed(double value, int digits) {
    if (std::isnan(value)) return "NaN";
    if (std::fabs(value) >= 1e21) return number_to_js_string(value);
    if (digits < 0) digits = 0;
    if (digits > 100) digits = 100;
    // Exact decimal expansion, then round half away from zero on the magnitude (toFixed picks the
    // larger n on a tie).
    std::vector<char> buffer(1200);
    std::snprintf(buffer.data(), buffer.size(), "%.1100f", std::fabs(value));
    std::string exact(buffer.data());
    auto dot = exact.find('.');
    std::string integer = exact.substr(0, dot);
    std::string fraction = exact.substr(dot + 1);
    std::string kept = integer + fraction.substr(0, static_cast<std::size_t>(digits));
    bool round_up = fraction[static_cast<std::size_t>(digits)] >= '5';
    if (round_up) {
        int at = static_cast<int>(kept.size()) - 1;
        while (at >= 0) {
            if (kept[static_cast<std::size_t>(at)] == '9') {
                kept[static_cast<std::size_t>(at)] = '0';
                --at;
            } else {
                ++kept[static_cast<std::size_t>(at)];
                break;
            }
        }
        if (at < 0) kept.insert(kept.begin(), '1');
    }
    std::size_t int_len = kept.size() - static_cast<std::size_t>(digits);
    std::string out = kept.substr(0, int_len);
    if (digits > 0) out += "." + kept.substr(int_len);
    if (value < 0) out = "-" + out;
    return out;
}

bool js_is_integer(double value) { return std::isfinite(value) && std::trunc(value) == value; }
bool js_is_safe_integer(double value) { return js_is_integer(value) && std::fabs(value) <= 9007199254740991.0; }

} // namespace lapilli::js_math
