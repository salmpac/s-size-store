#include "events.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ssize;

TEST_CASE("event queue drains what was pushed", "[events]") {
    EventQueue q;
    for (int i = 0; i < 5; ++i) {
        Event ev;
        ev.type = EventType::Impression;
        ev.item_id = i;
        CHECK(q.push(std::move(ev)));
    }

    std::vector<Event> batch;
    q.drain(batch);
    CHECK(batch.size() == 5);

    q.drain(batch);
    CHECK(batch.empty());
}

TEST_CASE("a full queue drops events instead of blocking", "[events]") {
    // Losing metrics under overload is the intended trade: a request must never
    // wait on the event buffer.
    EventQueue q(2);
    CHECK(q.push(Event{}));
    CHECK(q.push(Event{}));
    CHECK_FALSE(q.push(Event{}));
    CHECK(q.dropped() == 1);
}

TEST_CASE("ip hashing is stable, salted and irreversible in length", "[events]") {
    auto a = hash_ip("192.0.2.1", "secret");
    auto b = hash_ip("192.0.2.1", "secret");
    auto c = hash_ip("192.0.2.1", "other-secret");
    auto d = hash_ip("192.0.2.2", "secret");

    CHECK(a == b);        // same input, same bucket
    CHECK(a != c);        // salt matters
    CHECK(a != d);        // different address
    CHECK(a.size() >= 16);
}

TEST_CASE("user agents are classified", "[events]") {
    CHECK(classify_ua("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0)") == "mobile");
    CHECK(classify_ua("Mozilla/5.0 (Windows NT 10.0; Win64; x64)") == "desktop");
    CHECK(classify_ua("Mozilla/5.0 (compatible; YandexBot/3.0)") == "bot");
    CHECK(classify_ua("curl/8.0") == "bot");
    CHECK(classify_ua("") == "bot");
}
