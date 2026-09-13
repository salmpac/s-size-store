#include "links.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace ssize;

TEST_CASE("url_encode leaves unreserved characters alone", "[links]") {
    CHECK(url_encode("abcXYZ019-_.~") == "abcXYZ019-_.~");
}

TEST_CASE("url_encode escapes everything a query parameter would break on", "[links]") {
    CHECK(url_encode("https://ozon.ru/p?a=1&b=2") ==
          "https%3A%2F%2Fozon.ru%2Fp%3Fa%3D1%26b%3D2");
    CHECK(url_encode(" ") == "%20");
}

TEST_CASE("render_link substitutes the documented placeholders", "[links]") {
    LinkVars vars{
        .url      = "https://shop.example/p/1?ref=x",
        .subid    = "7Kq2mZ8xR1aP",
        .campaign = "abc123",
    };

    SECTION("direct template passes the url through untouched") {
        CHECK(render_link("{url}", vars) == "https://shop.example/p/1?ref=x");
    }

    SECTION("affiliate template encodes the landing url") {
        auto out = render_link(
            "https://ad.admitad.com/g/{campaign}/?subid={subid}&ulp={url_enc}", vars);
        CHECK(out ==
              "https://ad.admitad.com/g/abc123/?subid=7Kq2mZ8xR1aP"
              "&ulp=https%3A%2F%2Fshop.example%2Fp%2F1%3Fref%3Dx");
    }
}

TEST_CASE("render_link keeps unknown placeholders visible", "[links]") {
    // A typo in the DB should show up in the URL rather than silently dropping
    // a parameter the partner needs.
    LinkVars vars{.url = "u", .subid = "s", .campaign = "c"};
    CHECK(render_link("{url}?x={typo}", vars) == "u?x={typo}");
}

TEST_CASE("render_link tolerates an unterminated placeholder", "[links]") {
    LinkVars vars{.url = "u", .subid = "s", .campaign = "c"};
    CHECK(render_link("{url}?x={oops", vars) == "u?x={oops");
}

TEST_CASE("click tokens are unique and url-safe", "[links]") {
    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i) {
        auto t = make_click_token();
        REQUIRE(t.size() == 12);
        CHECK(url_encode(t) == t);          // must survive as a query parameter
        CHECK(seen.insert(t).second);       // no collisions at this scale
    }
}
