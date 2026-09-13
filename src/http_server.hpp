#pragma once

#include <memory>

namespace ssize {

struct Config;
class  CatalogHandle;
class  EventQueue;

// Accepts connections on one io_context thread, one stackful coroutine per
// connection. Handlers read the catalog snapshot directly and never block on
// the database, so a slow disk cannot back up the accept loop.
class HttpServer {
public:
    HttpServer(Config const& cfg, CatalogHandle& catalog, EventQueue& events);
    ~HttpServer();

    HttpServer(HttpServer const&) = delete;
    HttpServer& operator=(HttpServer const&) = delete;

    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ssize
