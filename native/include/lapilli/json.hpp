// JSON text in and out with the JavaScript semantics of the Node services:
//  - Json is nlohmann::ordered_json: object keys keep insertion order, as a JS object does.
//  - parse(): JSON.parse after the text and every value in it are charged to a Lorica budget
//    (bytes, values, nesting depth). Errors are JsonError{too_large | too_deep | invalid}.
//  - stringify(): JSON.stringify(value[, null, indent]): numbers as Number#toString, non-finite
//    numbers as null, integers beyond 2^53 printed as the double JSON.parse would have made.
//  Known difference: a number literal that overflows a double ("1e400") is `invalid` here
//  (JSON.parse gives Infinity).
#pragma once

#include "lapilli/budget.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lapilli {
using Json = nlohmann::ordered_json;
} // namespace lapilli

namespace lapilli::json {

using lapilli::Json;

struct JsonLimits {
    std::size_t max_bytes = 16u << 20;
    /** Values + keys (a container counts once). */
    std::size_t max_elements = 2u << 20;
    /** Nesting depth of arrays / objects ("[[1]]" has depth 2). */
    std::size_t max_depth = 256;

    /** The Lorica limits these map to (allocations: one per container plus the text). */
    [[nodiscard]] lorica::budget_limits budget() const noexcept {
        return {max_bytes, max_elements, max_elements == unlimited ? unlimited : max_elements + 1, max_depth};
    }
};

enum class JsonErrorKind { too_large, too_deep, invalid };

class JsonError : public std::runtime_error {
public:
    JsonError(JsonErrorKind kind, const std::string& what) : std::runtime_error(what), kind_(kind) {}
    [[nodiscard]] JsonErrorKind kind() const noexcept { return kind_; }

private:
    JsonErrorKind kind_;
};

/** "too_large" / "too_deep" / "invalid". */
const char* kind_name(JsonErrorKind kind) noexcept;

/** JSON.parse(text) within `limits`. Throws JsonError. */
Json parse(std::string_view text, const JsonLimits& limits = {});
/** parse() charging an existing budget (several documents of one input share it). Throws JsonError. */
Json parse(std::string_view text, lorica::input_budget& budget);
/** parse() that returns nullopt instead of throwing; `why` (optional) receives the message. */
std::optional<Json> try_parse(std::string_view text, const JsonLimits& limits = {}, std::string* why = nullptr,
                              JsonErrorKind* kind = nullptr);

/** JSON.stringify(value) (indent 0) or JSON.stringify(value, null, indent) (indent clamped to 0..10). */
std::string stringify(const Json& value, int indent = 0);

/** String(n) / Number#toString(): "1", "0.1", "1e+21", "-1.5e-7", "NaN", "Infinity", "-Infinity". */
std::string number_to_js_string(double value);
/** JSON.stringify(n): number_to_js_string for finite numbers, "null" otherwise. */
std::string number_to_json(double value);
/** Appends JSON.stringify(text) / JSON.stringify(number) to out. */
void append_json_string(std::string& out, std::string_view text);
void append_json_number(std::string& out, double value);

/** The JSON number as JavaScript reads it: a double (integers beyond 2^53 rounded as JSON.parse does); NaN for a non-number. */
double js_number_value(const Json& value);

} // namespace lapilli::json
