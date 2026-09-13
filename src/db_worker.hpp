#pragma once

#include "catalog.hpp"
#include "config.hpp"
#include "events.hpp"

#include <atomic>
#include <thread>

namespace ssize {

// The single database thread.
//
// It owns the only sqlite connection used at runtime and does exactly two jobs:
// rebuild the catalog snapshot on a timer, and flush queued events in batches.
// Request fibers never talk to it synchronously — they read the snapshot and
// drop events in the queue — so no HTTP request can ever block on disk I/O.
class DbWorker {
public:
    DbWorker(Config const& cfg, CatalogHandle& catalog, EventQueue& events,
             ConversionQueue& conversions);
    ~DbWorker();

    DbWorker(DbWorker const&) = delete;
    DbWorker& operator=(DbWorker const&) = delete;

    void start();
    void stop();

    // Rebuilds the snapshot synchronously. Used at startup so the server never
    // serves an empty catalog, and by tests.
    void refresh_now();

private:
    void run();
    void flush_events(class DbConn& conn);
    void flush_conversions(class DbConn& conn);
    void maintenance(class DbConn& conn);

    Config const&  cfg_;
    CatalogHandle& catalog_;
    EventQueue&    events_;
    ConversionQueue& conversions_;

    std::thread       thread_;
    std::atomic<bool> running_{false};
};

}  // namespace ssize
