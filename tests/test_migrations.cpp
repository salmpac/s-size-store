#include "db.hpp"
#include "migrations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using namespace ssize;

namespace {

std::filesystem::path repo_migrations() {
    // Set by CMake so the test works regardless of the build directory.
    if (auto* p = std::getenv("SSIZE_MIGRATIONS_DIR")) return p;
    return std::filesystem::path(SSIZE_SOURCE_DIR) / "migrations";
}

struct TempDb {
    std::filesystem::path path;
    TempDb() {
        path = std::filesystem::temp_directory_path() /
               ("ssize_test_" + std::to_string(::rand()) + ".db");
    }
    ~TempDb() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + "-wal", ec);
        std::filesystem::remove(path.string() + "-shm", ec);
    }
};

}  // namespace

TEST_CASE("migrations apply and are idempotent", "[migrations]") {
    TempDb tmp;
    DbConn conn(tmp.path);

    int v = apply_migrations(conn, repo_migrations());
    CHECK(v >= 1);

    // Running again must be a no-op rather than an error.
    int again = apply_migrations(conn, repo_migrations());
    CHECK(again == v);
}

TEST_CASE("the seeded direct partner exists after migration", "[migrations]") {
    TempDb tmp;
    DbConn conn(tmp.path);
    apply_migrations(conn, repo_migrations());

    auto p = conn.find_partner_by_code("direct");
    REQUIRE(p.has_value());
    CHECK(p->link_template == "{url}");
    CHECK(p->enabled);
}

TEST_CASE("an outbound click can be matched by a postback", "[migrations][metrics]") {
    TempDb tmp;
    DbConn conn(tmp.path);
    apply_migrations(conn, repo_migrations());

    Event ev;
    ev.ts = 1'700'000'000'000;
    ev.anon_id = "anon";
    ev.session_id = "sess";
    ev.type = EventType::Outbound;
    ev.item_id = 1;
    ev.position = 3;
    ev.click_token = "TESTTOKEN123";
    conn.insert_events(std::vector<Event>{ev});

    CHECK(conn.record_conversion("TESTTOKEN123", ev.ts + 1000, "order-1",
                                 499000, "approved", "raw=1"));

    // A postback for a click we never served must be rejected, or anyone could
    // invent conversions.
    CHECK_FALSE(conn.record_conversion("NEVERISSUED", ev.ts, "order-2",
                                       100, "approved", ""));
}
