// JavaScript number semantics the TS code relies on: Math.min / Math.max (NaN propagates, -0 <
// +0), Math.round (half up, toward +Infinity), Number(text), Number#toFixed, Number.isInteger.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::js_math {

double js_min(double a, double b);
double js_max(double a, double b);
/** Math.min(...values) (Infinity when empty). */
double js_min(const std::vector<double>& values);
double js_min(std::initializer_list<double> values);
/** Math.max(...values) (-Infinity when empty). */
double js_max(const std::vector<double>& values);
double js_max(std::initializer_list<double> values);
double js_round(double value);
/** Number(text): trims, "" -> 0, 0x/0o/0b prefixes, [+-]Infinity, otherwise NaN unless the whole text is a decimal literal. */
double js_to_number(std::string_view text);
/** Number#toFixed(digits) (0..100). */
std::string js_to_fixed(double value, int digits);
/** Number.isInteger. */
bool js_is_integer(double value);
/** Number.isSafeInteger. */
bool js_is_safe_integer(double value);

} // namespace lapilli::js_math
