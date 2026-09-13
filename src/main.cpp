#include "catalog.hpp"
#include "config.hpp"
#include "db.hpp"
#include "db_worker.hpp"
#include "events.hpp"
#include "http_server.hpp"
#include "migrations.hpp"

#include <boost/program_options.hpp>
#include <sodium.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace {

std::mutex              g_shutdown_mu;
std::condition_variable g_shutdown_cv;
std::atomic<bool>       g_shutdown{false};

void request_shutdown(int) {
    g_shutdown.store(true);
    g_shutdown_cv.notify_all();
}

}  // namespace

int main(int argc, char** argv) {
    namespace po = boost::program_options;

    std::string config_path;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "print help")
        ("config,c", po::value<std::string>(&config_path)->required(),
         "path to TOML config file");

    try {
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }
        po::notify(vm);
    } catch (std::exception const& e) {
        std::cerr << "usage: " << argv[0] << " --config <path>\n\n" << desc << "\n"
                  << "error: " << e.what() << std::endl;
        return 2;
    }

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid=%t] %v");
    if (auto* lvl = std::getenv("SSIZE_LOG_LEVEL")) {
        spdlog::set_level(spdlog::level::from_str(lvl));
    }
    spdlog::info("s-size starting, config={}", config_path);

    try {
        if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");

        auto cfg = ssize::load_config(config_path);

        {
            ssize::DbConn conn(cfg.db_path);
            int v = ssize::apply_migrations(conn, cfg.migrations_dir);
            spdlog::info("db schema at version {}", v);
        }

        ssize::CatalogHandle catalog;
        ssize::EventQueue    events(cfg.event_queue_capacity);

        ssize::DbWorker db(cfg, catalog, events);
        // Build the first snapshot before accepting traffic so the very first
        // visitor never sees an empty feed.
        db.refresh_now();
        db.start();

        ssize::HttpServer server(cfg, catalog, events);
        server.start();

        std::signal(SIGINT,  request_shutdown);
        std::signal(SIGTERM, request_shutdown);

        {
            std::unique_lock<std::mutex> lk(g_shutdown_mu);
            g_shutdown_cv.wait(lk, [] { return g_shutdown.load(); });
        }
        spdlog::info("shutdown requested");

        server.stop();
        db.stop();  // flushes the last batch of events
        spdlog::info("bye");
        return 0;
    } catch (std::exception const& e) {
        spdlog::critical("fatal: {}", e.what());
        return 1;
    }
}
