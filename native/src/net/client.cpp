#include "lapilli/http/client.hpp"

#include "lapilli/budget.hpp"
#include "lapilli/crypto.hpp"
#include "lapilli/http/address_policy.hpp"
#include "lapilli/http/tls.hpp"
#include "lapilli/text.hpp"
#include "lapilli/url.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <algorithm>
#include <array>
#include <memory>

namespace lapilli::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace bhttp = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

const char* kind_name(FetchErrorKind kind) noexcept {
    switch (kind) {
    case FetchErrorKind::invalid_url: return "invalid_url";
    case FetchErrorKind::unsupported_scheme: return "unsupported_scheme";
    case FetchErrorKind::credentials: return "credentials";
    case FetchErrorKind::blocked_address: return "blocked_address";
    case FetchErrorKind::dns: return "dns";
    case FetchErrorKind::connect: return "connect";
    case FetchErrorKind::tls: return "tls";
    case FetchErrorKind::timeout: return "timeout";
    case FetchErrorKind::cancelled: return "cancelled";
    case FetchErrorKind::too_large: return "too_large";
    case FetchErrorKind::header_too_large: return "header_too_large";
    case FetchErrorKind::protocol: return "protocol";
    case FetchErrorKind::redirect: return "redirect";
    }
    return "protocol";
}

std::string encode_form(const std::vector<FormPart>& parts, const std::string& boundary) {
    std::string body;
    for (const auto& part : parts) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + part.name + "\"; filename=\"" + part.filename + "\"\r\n";
        body += "Content-Type: " + part.content_type + "\r\n\r\n";
        body += part.bytes;
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

std::string random_boundary() { return "----lapilli" + crypto::random_hex(12); }

namespace {

[[noreturn]] void fail(FetchErrorKind kind, const std::string& what) { throw FetchError(kind, what); }

ssl::context& shared_tls_context() {
    static ssl::context context = [] {
        ssl::context c(ssl::context::tls_client);
        configure_client_tls(c.native_handle());
        return c;
    }();
    return context;
}

enum class Outcome { done, timed_out, cancelled };

/** Runs one asynchronous operation to its end, aborting it at the deadline or on cancel. */
class Driver {
public:
    explicit Driver(asio::io_context& io, const std::atomic<bool>* cancel) : io_(io), cancel_(cancel) {}

    template <class Start, class Abort>
    beast::error_code run(Start start, Clock::time_point deadline, Abort abort) {
        outcome_ = Outcome::done;
        bool done = false;
        beast::error_code result;
        start([&done, &result](beast::error_code ec, auto&&...) {
            result = ec;
            done = true;
        });
        while (!done) {
            const bool cancelled = cancel_ != nullptr && cancel_->load();
            if (cancelled || Clock::now() >= deadline) {
                abort();
                io_.restart();
                while (!done && io_.run_one() > 0) {
                }
                outcome_ = cancelled ? Outcome::cancelled : Outcome::timed_out;
                return result ? result : asio::error::operation_aborted;
            }
            io_.restart();
            const auto left = deadline - Clock::now();
            const auto slice = cancel_ != nullptr ? std::chrono::milliseconds(50) : std::chrono::milliseconds(1000);
            if (left < slice) io_.run_one_for(std::chrono::duration_cast<std::chrono::milliseconds>(left) + std::chrono::milliseconds(1));
            else io_.run_one_for(slice);
        }
        return result;
    }
    [[nodiscard]] Outcome outcome() const noexcept { return outcome_; }
    /** Throws for a timed-out / cancelled operation. */
    void check(const char* timed_out_message) const {
        if (outcome_ == Outcome::cancelled) fail(FetchErrorKind::cancelled, "the request was cancelled");
        if (outcome_ == Outcome::timed_out) fail(FetchErrorKind::timeout, timed_out_message);
    }

private:
    asio::io_context& io_;
    const std::atomic<bool>* cancel_;
    Outcome outcome_ = Outcome::done;
};

struct Target {
    url::Url url;
    bool tls = false;
    std::string host; // DNS / SNI / verification (IPv6 without brackets)
    std::uint16_t port = 0;
    std::string path; // pathname + search
};

Target target_of(const std::string& text, bool allow_credentials) {
    auto parsed = url::parse(text);
    if (!parsed) fail(FetchErrorKind::invalid_url, "the URL is not a URL");
    return [&] {
        if (parsed->protocol != "http:" && parsed->protocol != "https:") fail(FetchErrorKind::unsupported_scheme, "only http and https URLs are fetched");
        if (!allow_credentials && (!parsed->username.empty() || !parsed->password.empty())) fail(FetchErrorKind::credentials, "a URL with credentials is not fetched");
        Target target;
        target.tls = parsed->protocol == "https:";
        target.host = parsed->bare_hostname();
        target.port = parsed->effective_port();
        target.path = parsed->pathname + parsed->search;
        target.url = std::move(*parsed);
        return target;
    }();
}

std::string base64_credentials(const url::Url& u) {
    return crypto::base64_encode(url::percent_decode(u.username) + ":" + url::percent_decode(u.password));
}

/** The connection, plain or TLS. */
class Connection {
public:
    Connection(asio::io_context& io, Driver& driver) : io_(io), driver_(driver) {}

    beast::tcp_stream& lowest() { return tls_ ? beast::get_lowest_layer(*tls_) : *plain_; }

    void open(const Target& target, const ClientRequest& request, Clock::time_point deadline) {
        std::vector<tcp::endpoint> endpoints;
        if (request.pinned_addresses) {
            for (const auto& address : *request.pinned_addresses) {
                beast::error_code error;
                const auto ip = asio::ip::make_address(address, error);
                if (!error) endpoints.emplace_back(ip, target.port);
            }
            if (endpoints.empty()) fail(FetchErrorKind::dns, "no usable pinned address");
        } else if (is_ip_literal(target.host)) {
            endpoints.emplace_back(asio::ip::make_address(target.host), target.port);
        } else {
            tcp::resolver resolver(io_);
            tcp::resolver::results_type results;
            auto ec = driver_.run(
                [&](auto handler) {
                    resolver.async_resolve(target.host, std::to_string(target.port),
                                           [handler, &results](beast::error_code e, tcp::resolver::results_type r) mutable {
                                               results = std::move(r);
                                               handler(e);
                                           });
                },
                deadline, [&] { resolver.cancel(); });
            driver_.check("the host name could not be resolved in time");
            if (ec) fail(FetchErrorKind::dns, "the host name could not be resolved: " + ec.message());
            for (const auto& entry : results) endpoints.push_back(entry.endpoint());
        }
        if (request.address_policy == AddressPolicy::public_only) {
            // Every address must be public; the connection then goes only to these vetted addresses.
            for (const auto& endpoint : endpoints)
                if (!is_public_address(endpoint.address().to_string()))
                    fail(FetchErrorKind::blocked_address, "the address of " + target.host + " is not public");
        }
        if (target.tls) {
            tls_.emplace(io_, shared_tls_context());
            if (!set_server_name(tls_->native_handle(), target.host)) fail(FetchErrorKind::tls, "TLS: the server name could not be set");
            tls_->set_verify_mode(ssl::verify_peer);
            tls_->set_verify_callback(ssl::host_name_verification(target.host));
        } else {
            plain_.emplace(io_);
        }
        auto& socket = lowest();
        auto ec = driver_.run([&](auto handler) { socket.async_connect(endpoints, [handler](beast::error_code e, const tcp::endpoint&) mutable { handler(e); }); },
                              deadline, [&] { socket.close(); });
        driver_.check("the connection timed out");
        if (ec) fail(FetchErrorKind::connect, "could not connect: " + ec.message());
        if (tls_) {
            ec = driver_.run([&](auto handler) { tls_->async_handshake(ssl::stream_base::client, handler); }, deadline, [&] { socket.close(); });
            driver_.check("the TLS handshake timed out");
            if (ec) fail(FetchErrorKind::tls, "TLS handshake failed: " + ec.message());
        }
    }

    template <class Fn>
    beast::error_code with(Fn fn) {
        if (tls_) return fn(*tls_);
        return fn(*plain_);
    }

    void close() {
        beast::error_code ignored;
        lowest().socket().shutdown(tcp::socket::shutdown_both, ignored);
        lowest().close();
    }

private:
    asio::io_context& io_;
    Driver& driver_;
    std::optional<beast::tcp_stream> plain_;
    std::optional<ssl::stream<beast::tcp_stream>> tls_;
};

struct Hop {
    std::string method;
    Headers headers;
    std::string body;
    std::optional<std::vector<FormPart>> form;
};

bhttp::request<bhttp::string_body> build_request(const Target& target, const Hop& hop, const ClientRequest& request) {
    bhttp::request<bhttp::string_body> out;
    out.version(11);
    out.method_string(hop.method);
    out.target(target.path);
    out.set(bhttp::field::host, target.url.host());
    out.set(bhttp::field::user_agent, request.user_agent);
    out.set(bhttp::field::connection, "close");
    if (request.allow_url_credentials && (!target.url.username.empty() || !target.url.password.empty()))
        out.set(bhttp::field::authorization, "Basic " + base64_credentials(target.url));
    for (const auto& [name, value] : hop.headers) out.set(name, value);
    if (hop.form.has_value()) {
        const std::string boundary = random_boundary();
        out.set(bhttp::field::content_type, "multipart/form-data; boundary=" + boundary);
        out.body() = encode_form(*hop.form, boundary);
    } else {
        out.body() = hop.body;
    }
    if ((hop.method != "GET" && hop.method != "HEAD") || !out.body().empty()) out.prepare_payload();
    return out;
}

Headers headers_of(const bhttp::fields& fields) {
    Headers out;
    for (const auto& field : fields) out.emplace_back(text::ascii_lower(std::string(field.name_string())), std::string(field.value()));
    return out;
}

bool is_redirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

} // namespace

ClientResponse fetch(const ClientRequest& request) {
    const auto started = Clock::now();
    const auto total_deadline = request.timeout.count() > 0 ? started + request.timeout : Clock::time_point::max();
    Hop hop{request.method, request.headers, request.body, request.form};
    std::string current = request.url;
    bool redirected = false;
    for (int hops = 0;; ++hops) {
        const Target target = target_of(current, request.allow_url_credentials);
        asio::io_context io;
        Driver driver(io, request.cancel);
        Connection connection(io, driver);
        const auto now = Clock::now();
        const auto connect_deadline = request.connect_timeout.count() > 0 && now + request.connect_timeout < total_deadline ? now + request.connect_timeout : total_deadline;
        connection.open(target, request, connect_deadline);
        auto abort = [&] { connection.lowest().close(); };

        auto message = build_request(target, hop, request);
        auto ec = connection.with([&](auto& stream) { return driver.run([&](auto handler) { bhttp::async_write(stream, message, handler); }, total_deadline, abort); });
        driver.check("the request timed out");
        if (ec) fail(FetchErrorKind::protocol, "the request could not be sent: " + ec.message());

        beast::flat_buffer buffer;
        bhttp::response_parser<bhttp::buffer_body> parser;
        parser.header_limit(static_cast<std::uint32_t>(std::min<std::size_t>(request.header_limit, 0xFFFFFFFFu)));
        if (request.max_body == unlimited) parser.body_limit(boost::none);
        else parser.body_limit(static_cast<std::uint64_t>(request.max_body));
        if (hop.method == "HEAD") parser.skip(true);
        ec = connection.with([&](auto& stream) { return driver.run([&](auto handler) { bhttp::async_read_header(stream, buffer, parser, handler); }, total_deadline, abort); });
        driver.check("the response timed out");
        if (ec == bhttp::error::header_limit) fail(FetchErrorKind::header_too_large, "the response head is too large");
        if (ec == bhttp::error::body_limit) fail(FetchErrorKind::too_large, "the response body is larger than " + std::to_string(request.max_body) + " bytes");
        if (ec) fail(FetchErrorKind::protocol, "the response could not be read: " + ec.message());

        ClientResponse response;
        response.status = static_cast<int>(parser.get().result_int());
        response.headers = headers_of(parser.get().base());
        response.url = target.url.href();
        response.redirected = redirected;

        const auto location = response.header("location");
        if (is_redirect(response.status) && location && request.redirect != RedirectMode::manual) {
            if (request.redirect == RedirectMode::error) fail(FetchErrorKind::redirect, "the server answered with a redirect, which is not followed");
            if (hops + 1 > request.max_redirects) fail(FetchErrorKind::redirect, "too many redirects");
            const auto next = url::resolve(*location, target.url);
            if (!next) fail(FetchErrorKind::redirect, "the redirect location is not a URL");
            if (next->protocol != "http:" && next->protocol != "https:") fail(FetchErrorKind::unsupported_scheme, "a redirect to a scheme other than http and https is not followed");
            // Fetch's redirect rules: 303 (and 301 / 302 after POST) become GET without a body.
            if ((response.status == 303 && hop.method != "GET" && hop.method != "HEAD") ||
                ((response.status == 301 || response.status == 302) && hop.method == "POST")) {
                hop.method = "GET";
                hop.body.clear();
                hop.form.reset();
                for (const char* name : {"content-type", "content-length", "content-encoding", "content-language", "content-location"})
                    hop.headers.erase(std::remove_if(hop.headers.begin(), hop.headers.end(), [name](const auto& h) { return text::ascii_iequals(h.first, name); }), hop.headers.end());
            }
            if (next->origin() != target.url.origin()) {
                for (const char* name : {"authorization", "cookie", "proxy-authorization"})
                    hop.headers.erase(std::remove_if(hop.headers.begin(), hop.headers.end(), [name](const auto& h) { return text::ascii_iequals(h.first, name); }), hop.headers.end());
            }
            connection.close();
            current = next->href();
            redirected = true;
            continue;
        }

        if (request.on_head) request.on_head(response);
        lorica::input_budget budget(byte_limits(request.max_body));
        std::array<char, 64 * 1024> chunk{};
        const bool streaming = static_cast<bool>(request.on_chunk);
        while (!parser.is_done()) {
            parser.get().body().data = chunk.data();
            parser.get().body().size = chunk.size();
            ec = connection.with([&](auto& stream) { return driver.run([&](auto handler) { bhttp::async_read_some(stream, buffer, parser, handler); }, total_deadline, abort); });
            if (driver.outcome() == Outcome::cancelled && streaming) break; // a stream ends when cancelled
            driver.check("the response timed out");
            if (ec == bhttp::error::need_buffer) ec = {};
            if (ec == bhttp::error::body_limit) fail(FetchErrorKind::too_large, "the response body is larger than " + std::to_string(request.max_body) + " bytes");
            if (ec == bhttp::error::partial_message) fail(FetchErrorKind::protocol, "the response ended early");
            if (ec) fail(FetchErrorKind::protocol, "the response could not be read: " + ec.message());
            const std::size_t got = chunk.size() - parser.get().body().size;
            if (got == 0) continue;
            if (!budget.consume(got, 0, 0)) fail(FetchErrorKind::too_large, "the response body is larger than " + std::to_string(request.max_body) + " bytes");
            if (streaming) {
                if (!request.on_chunk(std::string_view(chunk.data(), got))) break;
            } else {
                response.body.append(chunk.data(), got);
            }
        }
        connection.close();
        return response;
    }
}

std::optional<ClientResponse> try_fetch(const ClientRequest& request, FetchFailure* failure) {
    try {
        return fetch(request);
    } catch (const FetchError& error) {
        if (failure != nullptr) *failure = FetchFailure{error.kind(), error.what()};
        return std::nullopt;
    } catch (const crypto::CryptoError& error) {
        if (failure != nullptr) *failure = FetchFailure{FetchErrorKind::protocol, error.what()};
        return std::nullopt;
    }
}

} // namespace lapilli::http
