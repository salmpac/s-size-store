#include "http_server.hpp"

#include "catalog.hpp"
#include "config.hpp"
#include "events.hpp"
#include "handlers.hpp"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace ssize {

namespace beast = boost::beast;
namespace http  = boost::beast::http;
namespace asio  = boost::asio;
using     tcp   = asio::ip::tcp;

// CONCURRENCY MODEL
// -----------------
// One io_context on one thread, one stackful coroutine per connection via
// asio::spawn. Those coroutines run on boost::context — the same primitive
// Boost.Fiber is built on — so this is "a fiber per connection" without the
// boost::fibers::asio bridge, which is broken in current Boost.
//
// Unlike the task tracker this is based on, handlers here never wait for the
// database: they read an immutable catalog snapshot and drop metrics events in
// a queue. The DB thread is a pure background worker.

struct HttpServer::Impl {
    Config const& cfg;
    std::shared_ptr<asio::io_context> io_ctx;
    std::unique_ptr<tcp::acceptor>    acceptor;
    std::thread       thread;
    std::atomic<bool> started{false};
    std::atomic<bool> stopped{false};

    HandlerContext handler_ctx;

    Impl(Config const& c, CatalogHandle& cat, EventQueue& ev)
        : cfg(c),
          io_ctx(std::make_shared<asio::io_context>(1)),
          handler_ctx{c, cat, ev} {}
};

HttpServer::HttpServer(Config const& cfg, CatalogHandle& catalog, EventQueue& events)
    : impl_(std::make_unique<Impl>(cfg, catalog, events)) {}

HttpServer::~HttpServer() { stop(); }

namespace {

std::string peer_ip(tcp::socket const& sock) {
    boost::system::error_code ec;
    auto ep = sock.remote_endpoint(ec);
    if (ec) return "0.0.0.0";
    return ep.address().to_string();
}

// Behind nginx the peer is always the proxy, so the real address comes from
// X-Forwarded-For. Trusting that header is only safe because nothing but the
// local reverse proxy can reach this port.
std::string client_address(HttpRequest const& req, std::string const& peer) {
    auto xff = req["X-Forwarded-For"];
    if (xff.empty()) return peer;
    auto comma = xff.find(',');
    auto first = comma == std::string_view::npos ? xff : xff.substr(0, comma);
    while (!first.empty() && first.front() == ' ') first.remove_prefix(1);
    return std::string(first);
}

void handle_connection(tcp::socket sock, HandlerContext& hctx, asio::yield_context yield) {
    std::string const peer = peer_ip(sock);
    try {
        for (;;) {
            beast::flat_buffer buffer;
            HttpRequest req;

            boost::system::error_code ec;
            http::async_read(sock, buffer, req, yield[ec]);
            if (ec == http::error::end_of_stream) break;
            if (ec == asio::error::eof || ec == asio::error::connection_reset) break;
            if (ec) {
                spdlog::debug("http read error: {}", ec.message());
                break;
            }

            HttpResponse res = dispatch(req, hctx, client_address(req, peer));
            res.prepare_payload();
            bool keep_alive = req.keep_alive();
            res.keep_alive(keep_alive);

            boost::system::error_code wec;
            http::async_write(sock, res, yield[wec]);
            if (wec) {
                spdlog::debug("http write error: {}", wec.message());
                break;
            }
            if (!keep_alive) break;
        }
    } catch (std::exception const& e) {
        spdlog::error("connection handler threw: {}", e.what());
    }
    boost::system::error_code ignore;
    sock.shutdown(tcp::socket::shutdown_send, ignore);
}

void accept_loop(std::shared_ptr<asio::io_context> io_ctx,
                 tcp::acceptor& acceptor,
                 HandlerContext& hctx,
                 std::atomic<bool>& stopped,
                 asio::yield_context yield) {
    for (;;) {
        tcp::socket sock(*io_ctx);
        boost::system::error_code ec;
        acceptor.async_accept(sock, yield[ec]);
        if (stopped.load()) break;
        if (ec == asio::error::operation_aborted) break;
        if (ec) {
            spdlog::warn("accept error: {}", ec.message());
            continue;
        }
        asio::spawn(*io_ctx,
            [s = std::move(sock), &hctx](asio::yield_context y) mutable {
                handle_connection(std::move(s), hctx, y);
            },
            asio::detached);
    }
}

}  // namespace

void HttpServer::start() {
    if (impl_->started.exchange(true)) return;

    impl_->thread = std::thread([this] {
        try {
            auto& io = *impl_->io_ctx;

            tcp::endpoint ep(asio::ip::make_address(impl_->cfg.bind_address),
                             impl_->cfg.port);
            impl_->acceptor = std::make_unique<tcp::acceptor>(io);
            impl_->acceptor->open(ep.protocol());
            impl_->acceptor->set_option(asio::socket_base::reuse_address(true));
            impl_->acceptor->bind(ep);
            impl_->acceptor->listen();

            spdlog::info("http listening on {}:{}{}",
                         impl_->cfg.bind_address, impl_->cfg.port, impl_->cfg.base_path);

            asio::spawn(io,
                [this](asio::yield_context yield) {
                    accept_loop(impl_->io_ctx, *impl_->acceptor,
                                impl_->handler_ctx, impl_->stopped, yield);
                },
                asio::detached);

            io.run();
        } catch (std::exception const& e) {
            spdlog::critical("HttpServer thread terminated: {}", e.what());
        }
    });
}

void HttpServer::stop() {
    if (impl_->stopped.exchange(true)) return;
    if (impl_->io_ctx) impl_->io_ctx->stop();
    if (impl_->thread.joinable()) impl_->thread.join();
}

}  // namespace ssize
