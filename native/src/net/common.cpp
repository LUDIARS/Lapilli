#include "lapilli/http/common.hpp"

#include "lapilli/text.hpp"

namespace lapilli::http {

std::optional<std::string> header_value(const Headers& headers, std::string_view name) {
    for (const auto& [key, value] : headers)
        if (text::ascii_iequals(key, name)) return value;
    return std::nullopt;
}

void set_header(Headers& headers, std::string_view name, std::string value) {
    bool placed = false;
    for (auto it = headers.begin(); it != headers.end();) {
        if (!text::ascii_iequals(it->first, name)) {
            ++it;
            continue;
        }
        if (!placed) {
            it->second = std::move(value);
            placed = true;
            ++it;
        } else {
            it = headers.erase(it);
        }
    }
    if (!placed) headers.emplace_back(std::string(name), std::move(value));
}

std::string_view reason_phrase(int status) noexcept {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "";
    }
}

} // namespace lapilli::http
