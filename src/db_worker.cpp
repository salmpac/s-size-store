#include "db_worker.hpp"

#include "db.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <vector>

namespace ssize {

namespace {

TsMs now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

DbWorker::DbWorker(Config const& cfg, CatalogHandle& catalog, EventQueue& events,
                   ConversionQueue& conversions)
    : cfg_(cfg), catalog_(catalog), events_(events), conversions_(conversions) {}

DbWorker::~DbWorker() { stop(); }

void DbWorker::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void DbWorker::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void DbWorker::refresh_now() {
    DbConn conn(cfg_.db_path);
    auto items    = conn.load_published_items();
    auto partners = conn.list_partners();
    auto snapshot = std::make_shared<Catalog const>(std::move(items), std::move(partners));
    spdlog::info("catalog snapshot: {} items", snapshot->size());
    catalog_.set(std::move(snapshot));
}

void DbWorker::run() {
    DbConn conn(cfg_.db_path);

    auto next_refresh     = std::chrono::steady_clock::now();
    auto next_maintenance = std::chrono::steady_clock::now() + std::chrono::hours(1);

    std::vector<Event> batch;

    while (running_.load()) {
        auto loop_start = std::chrono::steady_clock::now();

        if (loop_start >= next_refresh) {
            try {
                auto items    = conn.load_published_items();
                auto partners = conn.list_partners();
                auto snapshot = std::make_shared<Catalog const>(std::move(items),
                                                                std::move(partners));
                catalog_.set(std::move(snapshot));
            } catch (std::exception const& e) {
                // Keep serving the previous snapshot rather than going dark.
                spdlog::error("catalog refresh failed: {}", e.what());
            }
            next_refresh = loop_start + cfg_.refresh_interval;
        }

        try {
            flush_events(conn);
            // After the events: a postback can only match a click that has
            // already been written.
            flush_conversions(conn);
        } catch (std::exception const& e) {
            spdlog::error("flush failed: {}", e.what());
        }

        if (loop_start >= next_maintenance) {
            try {
                maintenance(conn);
            } catch (std::exception const& e) {
                spdlog::error("maintenance failed: {}", e.what());
            }
            next_maintenance = loop_start + std::chrono::hours(1);
        }

        // Sleeping on a fixed tick rather than blocking on the queue is what
        // keeps the refresh timer honest: a queue-blocking pop would stall the
        // snapshot refresh for as long as the site is idle.
        std::this_thread::sleep_for(cfg_.event_flush_interval);
    }

    // Last flush on the way out so a shutdown does not lose the final second
    // of clicks.
    try {
        flush_events(conn);
    } catch (std::exception const& e) {
        spdlog::error("final event flush failed: {}", e.what());
    }
}

void DbWorker::flush_events(DbConn& conn) {
    std::vector<Event> batch;
    events_.drain(batch);
    if (batch.empty()) return;

    conn.insert_events(batch);
    spdlog::debug("flushed {} events", batch.size());
}

void DbWorker::flush_conversions(DbConn& conn) {
    std::vector<Conversion> batch;
    conversions_.drain(batch);

    for (auto const& c : batch) {
        bool matched = conn.record_conversion(c.click_token, c.ts, c.order_ref,
                                              c.amount, c.status, c.raw);
        if (!matched) {
            // Either a forged postback or one for a click already pruned. Worth
            // seeing in the log either way.
            spdlog::warn("postback for unknown click token '{}' (order '{}')",
                         c.click_token, c.order_ref);
        }
    }
    if (!batch.empty()) spdlog::info("recorded {} postbacks", batch.size());
}

void DbWorker::maintenance(DbConn& conn) {
    auto const now = now_ms();
    std::int64_t const today = now / 86'400'000;

    // Re-roll the last two days: yesterday can still receive late postbacks and
    // events that arrived after the previous rollup.
    conn.rollup_daily(today - 1, today);

    auto cutoff = now - static_cast<TsMs>(cfg_.event_retention_days) * 86'400'000;
    int removed = conn.prune_events(cutoff);
    if (removed > 0) spdlog::info("pruned {} events older than {} days",
                                  removed, cfg_.event_retention_days);
}

}  // namespace ssize
