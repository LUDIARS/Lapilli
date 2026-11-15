#include "lapilli/http/server.hpp"

#include "lapilli/text.hpp"

#include <stdexcept>

namespace lapilli::http {

int resolve_port(const std::optional<std::string>& value, int default_port, std::string_view var_name) {
    if (!value.has_value()) return default_port;
    const std::string trimmed = text::js_trim(*value);
    if (trimmed.empty()) return default_port;
    bool digits = true;
    for (char c : trimmed)
        if (c < '0' || c > '9') digits = false;
    // More than 6 digits is out of range either way.
    long long port = -1;
    if (digits && trimmed.size() <= 6) port = std::stoll(trimmed);
    if (!digits || port < 1 || port > 65535)
        throw std::invalid_argument(std::string(var_name) + " must be an integer between 1 and 65535 (got '" + *value + "')");
    return static_cast<int>(port);
}

} // namespace lapilli::http
