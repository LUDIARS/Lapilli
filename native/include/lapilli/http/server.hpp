// An HTTP/1.1 server (Boost.Beast) for the services' APIs and health probes. Loopback by default.
//
// Limits (all inputs bounded before they are stored):
//   header_bytes            request line + headers; over -> 431 and the connection closes
//   body_bytes              request body (Content-Length or chunked); over -> 413, closes
//   max_connections         open connections; one more is closed at once without reading (reject)
//   read_timeout            to receive one whole request (idle keep-alive included); then closes
//   write_timeout           to send one response
//   handler_timeout         a handler that has not answered by then gets 503 (0 = no limit)
//   requests_per_connection then `Connection: close`
// Broken requests get 400. A handler that throws, or drops its Responder without answering, gets 500.
//
// Handlers may answer later and from any thread: they receive the Request and a Responder whose
// send() can be called once (copies share it). HEAD responses keep Content-Length and send no body.
#pragma once

#include "lapilli/http/common.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lapilli::http {

struct ServerRequest {
    std::string method;
    /** As on the request line ("/a/b?x=1"). */
    std::string target;
    /** target up to '?'. */
    std::string path;
    /** After '?' (no '?'), or empty. */
    std::string query;
    Headers headers;
    std::string body;
    std::string remote_address;
    std::uint16_t remote_port = 0;
    /** 11 for HTTP/1.1, 10 for HTTP/1.0. */
    int version = 11;
    bool keep_alive = true;

    [[nodiscard]] std::optional<std::string> header(std::string_view name) const { return header_value(headers, name); }
};

struct ServerResponse {
    int status = 200;
    Headers headers;
    std::string body;

    /** status + `Content-Type: application/json; charset=utf-8` + body. */
    static ServerResponse json(int status, std::string body);
    static ServerResponse text(int status, std::string body, std::string content_type = "text/plain; charset=utf-8");
};

class Responder {
public:
    struct State;
    explicit Responder(std::shared_ptr<State> state) : state_(std::move(state)) {}
    /** Sends the response (first call wins; later calls return false). Thread safe. */
    bool send(ServerResponse response) const;
    /** True once a response was sent (by send, a timeout or an error). */
    [[nodiscard]] bool sent() const;

private:
    std::shared_ptr<State> state_;
};

using Handler = std::function<void(ServerRequest&& request, Responder responder)>;
using SyncHandler = std::function<ServerResponse(const ServerRequest& request)>;
/** Adapts a synchronous handler (runs on the I/O thread: keep it short). */
Handler synchronous(SyncHandler handler);

struct ServerLimits {
    std::size_t header_bytes = 8 * 1024;
    std::size_t body_bytes = 1024 * 1024;
    std::size_t max_connections = 64;
    std::chrono::milliseconds read_timeout{10000};
    std::chrono::milliseconds write_timeout{10000};
    std::chrono::milliseconds handler_timeout{0};
    std::size_t requests_per_connection = 100;
};

struct ServerOptions {
    /** Loopback unless the caller decides otherwise. */
    std::string host = "127.0.0.1";
    /** 0 = an ephemeral port (listen() returns it). */
    std::uint16_t port = 0;
    ServerLimits limits{};
    /** I/O threads (run() uses the caller's thread as one of them). */
    unsigned threads = 1;
};

class ServerError : public std::runtime_error {
public:
    explicit ServerError(const std::string& what) : std::runtime_error(what) {}
};

class Server {
public:
    Server(ServerOptions options, Handler handler);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /** Binds and listens; returns the bound port. Throws ServerError (bad host, port in use). */
    std::uint16_t listen();
    /** SIGINT / SIGTERM stop the server (call before run() / start()). */
    void stop_on_signals();
    /** Serves on the calling thread (plus threads - 1 more) until stop(). listen() first. */
    void run();
    /** Serves on background threads; returns at once. listen() first. */
    void start();
    /** Stops accepting and serving (thread safe, also from a handler). */
    void stop();
    /** Waits for start()ed threads to end (after stop()). */
    void wait();
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::size_t open_connections() const noexcept;

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

/**
 * The port from an environment value: unset or blank -> default_port; otherwise digits only, 1..65535.
 * Throws std::invalid_argument("<var_name> must be an integer between 1 and 65535 (got '<value>')").
 */
int resolve_port(const std::optional<std::string>& value, int default_port, std::string_view var_name);

} // namespace lapilli::http
