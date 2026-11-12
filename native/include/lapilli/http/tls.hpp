// The TLS client setup every LUDIARS service uses, for the shared client and for callers that keep
// their own Asio ssl::context (pass context.native_handle()).
#pragma once

#include <string>

struct ssl_ctx_st;
struct ssl_st;

namespace lapilli::http {

/**
 * TLS 1.2 minimum, peer verification on, OpenSSL's default verify paths and, on Windows, every
 * certificate of the system ROOT store (OpenSSL has no Windows trust store of its own).
 * Returns the number of Windows ROOT certificates added (0 elsewhere).
 */
int configure_client_tls(ssl_ctx_st* context);
/** SNI for a host name; nothing for an IP address literal (RFC 6066). False when OpenSSL refused. */
bool set_server_name(ssl_st* ssl, const std::string& host);
/** True for an IPv4 / IPv6 literal (no brackets). */
bool is_ip_literal(const std::string& host);

} // namespace lapilli::http
