// Types shared by the HTTP server and client.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lapilli::http {

/** Header name -> value, in order. Lookups are case-insensitive. */
using Headers = std::vector<std::pair<std::string, std::string>>;

/** The first value of `name` (case-insensitive). */
std::optional<std::string> header_value(const Headers& headers, std::string_view name);
/** Replaces every `name` field with one (or appends it). */
void set_header(Headers& headers, std::string_view name, std::string value);

/** The standard reason phrase ("OK", "Not Found"); "" for an unknown code. */
std::string_view reason_phrase(int status) noexcept;

} // namespace lapilli::http
