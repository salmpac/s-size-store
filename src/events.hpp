#pragma once

#include "models.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ssize {

// Buffer between request fibers (producers) and the DB thread (single consumer).
//
// Handlers must never wait on the database: a click is logged by dropping it in
// here and returning the redirect immediately. The DB thread drains the buffer
// once a second and writes the batch in one transaction.
//
// Deliberately a plain mutex + double buffer rather than a fiber channel: the
// producers live on a different thread than the consumer, the critical section
// is a push_back, and swapping the whole vector keeps the lock off the actual
// disk write.
class EventQueue {
public:
    explicit EventQueue(std::size_t max_pending = 100'000)
        : max_pending_(max_pending) {}

    // Returns false if the buffer is full — the event is dropped rather than
    // blocking a request. Metrics loss under overload beats a stalled site.
    bool push(Event ev);

    // Moves everything pending into `out` (cleared first). Called by the DB
    // thread; never blocks on I/O while holding the lock.
    void drain(std::vector<Event>& out);

    std::size_t dropped() const noexcept;

private:
    mutable std::mutex mu_;
    std::vector<Event> pending_;
    std::size_t        max_pending_;
    std::size_t        dropped_{0};
};

// Salted hash of a client IP. The raw address is never stored: under 152-ФЗ it
// is personal data, and we only ever need equality (unique visitors, abuse).
std::vector<std::uint8_t> hash_ip(std::string_view ip, std::string_view secret);

// Coarse client classification from the User-Agent: mobile|desktop|bot.
// Crawlers are 30-50% of raw traffic; without this every funnel number is wrong.
std::string classify_ua(std::string_view user_agent);

}  // namespace ssize
