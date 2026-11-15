#include "lapilli/http/server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <mutex>
#include <thread>
#include <vector>

namespace lapilli::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace bhttp = beast::http;
using tcp = asio::ip::tcp;

namespace {
constexpr const char* kJsonType = "application/json; charset=utf-8";
} // namespace

ServerResponse ServerResponse::json(int status, std::string body) {
    ServerResponse out;
    out.status = status;
    out.headers.emplace_back("Content-Type", kJsonType);
    out.body = std::move(body);
    return out;
}

ServerResponse ServerResponse::text(int status, std::string body, std::string content_type) {
    ServerResponse out;
    out.status = status;
    out.headers.emplace_back("Content-Type", std::move(content_type));
    out.body = std::move(body);
    return out;
}

Handler synchronous(SyncHandler handler) {
    return [handler = std::move(handler)](ServerRequest&& request, Responder responder) { responder.send(handler(request)); };
}

// Named (not anonymous) because Responder::State and Server::Impl hold these types.
namespace server_detail {
class Session;
struct Shared;
} // namespace server_detail
using server_detail::Session;
using server_detail::Shared;

struct Responder::State {
    std::shared_ptr<Session> session;
    std::atomic<bool> sent{false};
    explicit State(std::shared_ptr<Session> owner) : session(std::move(owner)) {}
    ~State();
    /** Delivers `response` to the session unless something was already sent. */
    bool deliver(ServerResponse response);
};

namespace server_detail {
/** What sessions share with the server; outlives the server while a session still exists. */
struct Shared {
    ServerOptions options;
    Handler handler;
    std::atomic<std::size_t> open{0};
    std::atomic<bool> stopping{false};
    std::mutex sessions_mutex;
    std::vector<std::weak_ptr<Session>> sessions;
};
} // namespace server_detail

struct Server::Impl {
    std::shared_ptr<Shared> shared;
    std::shared_ptr<asio::io_context> io;
    tcp::acceptor acceptor;
    std::optional<asio::signal_set> signals;
    std::uint16_t bound = 0;
    std::vector<std::thread> threads;
    std::mutex threads_mutex;

    Impl(ServerOptions options, Handler handler)
        : shared(std::make_shared<Shared>()),
          io(std::make_shared<asio::io_context>(static_cast<int>(std::max(1u, options.threads)))),
          acceptor(*io) {
        shared->options = std::move(options);
        shared->handler = std::move(handler);
    }
    void accept();
    void halt();
};

namespace server_detail {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::shared_ptr<Shared> server, std::shared_ptr<asio::io_context> io)
        : io_(std::move(io)), stream_(std::move(socket)), server_(std::move(server)) {
        beast::error_code ignored;
        const auto remote = stream_.socket().remote_endpoint(ignored);
        if (!ignored) {
            remote_address_ = remote.address().to_string();
            remote_port_ = remote.port();
        }
    }
    ~Session() { server_->open.fetch_sub(1); }

    void start() {
        asio::dispatch(stream_.get_executor(), [self = shared_from_this()] { self->read(); });
    }

    /** Closes the connection (server shutdown; no I/O thread is running then). */
    void abort() { close(); }
    [[nodiscard]] bool stopping() const { return server_->stopping.load(); }

    /** Called on any thread: hands the response to the session's strand. */
    void post_response(ServerResponse response) {
        asio::post(stream_.get_executor(), [self = shared_from_this(), response = std::move(response)]() mutable {
            self->write(std::move(response), self->keep_alive_);
        });
    }

private:
    const ServerLimits& limits() const { return server_->options.limits; }

    void read() {
        if (server_->stopping) return close();
        parser_.emplace();
        parser_->header_limit(static_cast<std::uint32_t>(std::min<std::size_t>(limits().header_bytes, 0xFFFFFFFFu)));
        parser_->body_limit(static_cast<std::uint64_t>(limits().body_bytes));
        stream_.expires_after(limits().read_timeout);
        bhttp::async_read(stream_, buffer_, *parser_, [self = shared_from_this()](beast::error_code error, std::size_t) { self->on_read(error); });
    }

    void error_response(int status, const char* code) {
        ServerResponse response = ServerResponse::json(status, std::string(R"({"error":")") + code + "\"}");
        head_ = false;
        write(std::move(response), false);
    }

    void on_read(beast::error_code error) {
        if (error == bhttp::error::body_limit) return error_response(413, "payload_too_large");
        if (error == bhttp::error::header_limit) return error_response(431, "header_too_large");
        if (error == bhttp::error::end_of_stream || error == beast::error::timeout || error == asio::error::operation_aborted ||
            error == asio::error::connection_reset || error == asio::error::eof) {
            return close();
        }
        if (error) {
            // A parse error before anything was read is a broken request: 400 (others just close).
            return error_response(400, "bad_request");
        }
        auto message = parser_->release();
        ++served_;
        keep_alive_ = message.keep_alive() && served_ < limits().requests_per_connection && !server_->stopping;
        head_ = message.method() == bhttp::verb::head;
        stream_.expires_never();

        ServerRequest request;
        request.method = std::string(message.method_string());
        request.target = std::string(message.target());
        const auto question = request.target.find('?');
        request.path = request.target.substr(0, question);
        if (question != std::string::npos) request.query = request.target.substr(question + 1);
        for (const auto& field : message) request.headers.emplace_back(std::string(field.name_string()), std::string(field.value()));
        request.body = std::move(message.body());
        request.remote_address = remote_address_;
        request.remote_port = remote_port_;
        request.version = static_cast<int>(message.version());
        request.keep_alive = message.keep_alive();

        auto state = std::make_shared<Responder::State>(shared_from_this());
        state_ = state;
        if (limits().handler_timeout.count() > 0) {
            timer_.emplace(stream_.get_executor());
            timer_->expires_after(limits().handler_timeout);
            timer_->async_wait([weak = std::weak_ptr<Responder::State>(state)](beast::error_code wait_error) {
                if (wait_error) return;
                if (auto alive = weak.lock()) alive->deliver(ServerResponse::json(503, R"({"error":"handler_timeout"})"));
            });
        }
        try {
            server_->handler(std::move(request), Responder(std::move(state)));
        } catch (...) {
            if (auto alive = state_.lock()) alive->deliver(ServerResponse::json(500, R"({"error":"internal_error"})"));
        }
    }

    void write(ServerResponse response, bool keep_alive) {
        if (timer_) timer_->cancel();
        auto message = std::make_shared<bhttp::response<bhttp::string_body>>(static_cast<bhttp::status>(response.status), 11);
        for (const auto& [name, value] : response.headers) message->insert(name, value);
        message->keep_alive(keep_alive);
        message->body() = std::move(response.body);
        message->prepare_payload();
        if (head_) message->body().clear(); // HEAD: Content-Length stays, no body
        stream_.expires_after(limits().write_timeout);
        // Beast's serializer would send the body for HEAD; skip it by writing the header only.
        auto done = [self = shared_from_this(), message, keep_alive](beast::error_code error, std::size_t) {
            if (error || !keep_alive) return self->close();
            self->read();
        };
        if (head_) {
            auto serializer = std::make_shared<bhttp::response_serializer<bhttp::string_body>>(*message);
            bhttp::async_write_header(stream_, *serializer, [serializer, done = std::move(done)](beast::error_code error, std::size_t n) mutable { done(error, n); });
        } else {
            bhttp::async_write(stream_, *message, std::move(done));
        }
    }

    void close() {
        beast::error_code ignored;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
        stream_.close();
    }

    // The io_context outlives the socket even when a Responder keeps the session past the server.
    std::shared_ptr<asio::io_context> io_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_{8 * 1024};
    std::optional<bhttp::request_parser<bhttp::string_body>> parser_;
    std::shared_ptr<Shared> server_;
    std::optional<asio::steady_timer> timer_;
    std::weak_ptr<Responder::State> state_;
    std::string remote_address_;
    std::uint16_t remote_port_ = 0;
    std::size_t served_ = 0;
    bool keep_alive_ = false;
    bool head_ = false;
};

} // namespace server_detail

Responder::State::~State() {
    // Dropped without an answer: 500 rather than a connection that hangs until it times out.
    if (!sent.exchange(true) && session && !session->stopping()) session->post_response(ServerResponse::json(500, R"({"error":"no_response"})"));
}

bool Responder::State::deliver(ServerResponse response) {
    if (sent.exchange(true)) return false;
    if (session->stopping()) return false; // the server is gone: the connection is closed
    session->post_response(std::move(response));
    return true;
}

bool Responder::send(ServerResponse response) const { return state_ && state_->deliver(std::move(response)); }
bool Responder::sent() const { return !state_ || state_->sent.load(); }

void Server::Impl::accept() {
    acceptor.async_accept(asio::make_strand(*io), [this](beast::error_code error, tcp::socket socket) {
        if (error == asio::error::operation_aborted || shared->stopping) return;
        if (!error) {
            // Over the cap the connection is closed at once, nothing read (reject).
            if (shared->open.load() >= shared->options.limits.max_connections) {
                beast::error_code ignored;
                socket.close(ignored);
            } else {
                shared->open.fetch_add(1);
                auto session = std::make_shared<Session>(std::move(socket), shared, io);
                {
                    std::lock_guard lock(shared->sessions_mutex);
                    auto& list = shared->sessions;
                    list.erase(std::remove_if(list.begin(), list.end(), [](const std::weak_ptr<Session>& w) { return w.expired(); }), list.end());
                    list.push_back(session);
                }
                session->start();
            }
        }
        accept();
    });
}

void Server::Impl::halt() {
    shared->stopping = true;
    io->stop();
}

Server::Server(ServerOptions options, Handler handler) {
    if (!handler) throw ServerError("a server needs a handler");
    impl_ = std::make_shared<Impl>(std::move(options), std::move(handler));
}

Server::~Server() {
    stop();
    wait();
    // No I/O thread runs now: close everything and let the aborted operations finish, so no
    // handler is left holding a session (or this server) when the members are destroyed.
    beast::error_code ignored;
    impl_->acceptor.close(ignored);
    if (impl_->signals) impl_->signals->cancel(ignored);
    std::vector<std::shared_ptr<Session>> live;
    {
        std::lock_guard lock(impl_->shared->sessions_mutex);
        for (auto& weak : impl_->shared->sessions)
            if (auto session = weak.lock()) live.push_back(std::move(session));
        impl_->shared->sessions.clear();
    }
    for (auto& session : live) session->abort();
    live.clear();
    impl_->io->restart();
    impl_->io->run();
}

std::uint16_t Server::listen() {
    const auto& options = impl_->shared->options;
    beast::error_code error;
    const auto address = asio::ip::make_address(options.host, error);
    if (error) throw ServerError("invalid host " + options.host);
    const tcp::endpoint endpoint(address, options.port);
    auto& acceptor = impl_->acceptor;
    acceptor.open(endpoint.protocol(), error);
    if (!error) acceptor.set_option(asio::socket_base::reuse_address(true), error);
    if (!error) acceptor.bind(endpoint, error);
    if (!error) acceptor.listen(asio::socket_base::max_listen_connections, error);
    if (error) {
        beast::error_code ignored;
        acceptor.close(ignored);
        throw ServerError("cannot listen on " + options.host + ":" + std::to_string(options.port) + ": " + error.message());
    }
    impl_->bound = acceptor.local_endpoint().port();
    impl_->accept();
    return impl_->bound;
}

void Server::stop_on_signals() {
    impl_->signals.emplace(*impl_->io, SIGINT, SIGTERM);
    impl_->signals->async_wait([impl = impl_.get()](beast::error_code error, int) {
        if (!error) impl->halt();
    });
}

void Server::run() {
    if (!impl_->acceptor.is_open()) throw ServerError("listen() first");
    std::vector<std::thread> extra;
    const auto io = impl_->io;
    for (unsigned i = 1; i < std::max(1u, impl_->shared->options.threads); ++i) extra.emplace_back([io] { io->run(); });
    io->run();
    for (auto& thread : extra) thread.join();
}

void Server::start() {
    if (!impl_->acceptor.is_open()) throw ServerError("listen() first");
    std::lock_guard lock(impl_->threads_mutex);
    const auto io = impl_->io;
    for (unsigned i = 0; i < std::max(1u, impl_->shared->options.threads); ++i) impl_->threads.emplace_back([io] { io->run(); });
}

void Server::stop() { impl_->halt(); }

void Server::wait() {
    std::vector<std::thread> threads;
    {
        std::lock_guard lock(impl_->threads_mutex);
        threads.swap(impl_->threads);
    }
    for (auto& thread : threads) {
        if (thread.get_id() == std::this_thread::get_id()) thread.detach();
        else if (thread.joinable()) thread.join();
    }
}

std::uint16_t Server::port() const noexcept { return impl_->bound; }
std::size_t Server::open_connections() const noexcept { return impl_->shared->open.load(); }

} // namespace lapilli::http
