// A synchronous HTTP/1.1 client over Boost.Beast and OpenSSL (the services' replacement for fetch).
//   - http and https only; URL credentials are refused unless allowed;
//   - TLS 1.2+, the peer chain verified against the system roots (OpenSSL's default paths, plus the
//     Windows ROOT store), the host name verified, SNI sent for names (not for IP addresses);
//   - redirects: manual (3xx returned), error (3xx -> FetchError redirect) or follow (up to
//     max_redirects hops; 301/302 POST and 303 become GET; Authorization / Cookie dropped when the
//     origin changes); every hop passes the address policy again;
//   - address policy: allow, or public_only (SSRF guard: every resolved address must be public, and
//     the connection goes to exactly those vetted addresses, so DNS cannot change in between);
//   - deadlines: connect (DNS + TCP + TLS) and the whole exchange; cancellation by an atomic flag;
//   - response head capped (header_limit) and body charged to a Lorica budget (max_body);
//   - streaming: on_head / on_chunk see the body as it arrives instead of accumulating it.
// Error messages never contain header values, bodies or URL credentials.
#pragma once

#include "lapilli/http/common.hpp"
#include "lapilli/json.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lapilli::http {

/** One part of a multipart/form-data body (FormData#append with a Blob). */
struct FormPart {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string bytes;
};

enum class RedirectMode { manual, error, follow };
enum class AddressPolicy { allow, public_only };

struct ClientResponse;

struct ClientRequest {
    std::string method = "GET";
    std::string url;
    Headers headers;
    std::string body;
    /** A multipart/form-data body instead of `body` (the client writes the content-type with a random boundary). */
    std::optional<std::vector<FormPart>> form;
    RedirectMode redirect = RedirectMode::manual;
    int max_redirects = 5;
    AddressPolicy address_policy = AddressPolicy::allow;
    /** Connect only to these addresses (no DNS lookup); still checked against the policy. */
    std::optional<std::vector<std::string>> pinned_addresses;
    bool allow_url_credentials = false;
    std::chrono::milliseconds connect_timeout{15000};
    /** The whole exchange, all hops. Zero or less: none (cancel only; for streams). */
    std::chrono::milliseconds timeout{30000};
    std::size_t header_limit = 64u << 10;
    /** Response body bytes (lapilli::unlimited for an endless stream). */
    std::size_t max_body = 16u << 20;
    std::string user_agent = "lapilli";
    const std::atomic<bool>* cancel = nullptr;
    /** Streaming: called once with the head (body empty) of the final response. */
    std::function<void(const ClientResponse& head)> on_head;
    /** Streaming: each body piece; return false to stop reading (fetch returns normally). */
    std::function<bool(std::string_view chunk)> on_chunk;
};

struct ClientResponse {
    int status = 0;
    /** Lower-case names, in order. */
    Headers headers;
    std::string body;
    /** The URL of this response (after redirects). */
    std::string url;
    bool redirected = false;

    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status <= 299; }
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const { return header_value(headers, name); }
    /** The body as JSON (charged to `limits`). Throws json::JsonError. */
    [[nodiscard]] Json json(const json::JsonLimits& limits = {}) const { return json::parse(body, limits); }
};

enum class FetchErrorKind {
    invalid_url,
    unsupported_scheme,
    credentials,
    blocked_address,
    dns,
    connect,
    tls,
    timeout,
    cancelled,
    too_large,
    header_too_large,
    protocol,
    redirect,
};
const char* kind_name(FetchErrorKind kind) noexcept;

class FetchError : public std::runtime_error {
public:
    FetchError(FetchErrorKind kind, const std::string& what) : std::runtime_error(what), kind_(kind) {}
    [[nodiscard]] FetchErrorKind kind() const noexcept { return kind_; }

private:
    FetchErrorKind kind_;
};

struct FetchFailure {
    FetchErrorKind kind = FetchErrorKind::protocol;
    std::string message;
};

/** Performs the request. Throws FetchError. */
ClientResponse fetch(const ClientRequest& request);
/** fetch() that reports failures instead of throwing (callbacks' own exceptions still propagate). */
std::optional<ClientResponse> try_fetch(const ClientRequest& request, FetchFailure* failure = nullptr);

/** The multipart/form-data body of `parts` with `boundary`. */
std::string encode_form(const std::vector<FormPart>& parts, const std::string& boundary);
/** A boundary with 24 random hex digits (OpenSSL RAND_bytes). */
std::string random_boundary();

} // namespace lapilli::http
