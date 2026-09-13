#include "handlers.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ssize;

TEST_CASE("parse_query decodes both keys and values", "[handlers]") {
    auto q = parse_query("tag=techwear&p=4&utm_source=tg%20channel");
    CHECK(q.at("tag") == "techwear");
    CHECK(q.at("p") == "4");
    CHECK(q.at("utm_source") == "tg channel");
}

TEST_CASE("parse_query handles empty and valueless parameters", "[handlers]") {
    auto q = parse_query("a&b=&c=1");
    CHECK(q.at("a").empty());
    CHECK(q.at("b").empty());
    CHECK(q.at("c") == "1");
    CHECK(parse_query("").empty());
}

TEST_CASE("url_decode handles plus and percent forms", "[handlers]") {
    CHECK(url_decode("a+b") == "a b");
    CHECK(url_decode("%D1%85") == "\xD1\x85");   // 'х' in UTF-8
    CHECK(url_decode("100%") == "100%");         // trailing % is not an escape
}

TEST_CASE("cookie_value picks the right cookie", "[handlers]") {
    std::string header = "aid=abc123; sid=def456; other=zzz";
    CHECK(cookie_value(header, "aid") == "abc123");
    CHECK(cookie_value(header, "sid") == "def456");
    CHECK(cookie_value(header, "missing").empty());
}

TEST_CASE("cookie_value does not match a name that is only a suffix", "[handlers]") {
    // 'id' must not match 'aid', or one visitor's cookie would be read as
    // another's.
    CHECK(cookie_value("aid=abc", "id").empty());
}

TEST_CASE("split_target separates path from query", "[handlers]") {
    auto t = split_target("/r/8123?p=4&s=feed");
    CHECK(t.path == "/r/8123");
    CHECK(t.query == "p=4&s=feed");

    auto plain = split_target("/item/1");
    CHECK(plain.path == "/item/1");
    CHECK(plain.query.empty());
}
