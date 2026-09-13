#include "catalog.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ssize;

namespace {

Item make_item(std::int64_t id, double score, TsMs published,
               std::vector<std::string> tags = {},
               std::vector<std::string> sizes = {}) {
    Item it;
    it.id = id;
    it.score = score;
    it.published_at = published;
    it.tags = std::move(tags);
    it.sizes = std::move(sizes);
    it.status = ItemStatus::Published;
    return it;
}

}  // namespace

TEST_CASE("catalog orders by score, then recency", "[catalog]") {
    Catalog cat({make_item(1, 1.0, 100),
                 make_item(2, 5.0, 50),
                 make_item(3, 5.0, 90)}, {});

    auto feed = cat.feed();
    REQUIRE(feed.size() == 3);
    CHECK(feed[0].id == 3);   // same score as 2, but newer
    CHECK(feed[1].id == 2);
    CHECK(feed[2].id == 1);
}

TEST_CASE("catalog looks items up by id", "[catalog]") {
    Catalog cat({make_item(7, 0, 0), make_item(9, 0, 0)}, {});
    REQUIRE(cat.find(7) != nullptr);
    CHECK(cat.find(7)->id == 7);
    CHECK(cat.find(123) == nullptr);
}

TEST_CASE("catalog filters by tag and size", "[catalog]") {
    Catalog cat({make_item(1, 0, 0, {"techwear"}, {"M"}),
                 make_item(2, 0, 0, {"y2k"},      {"M", "L"}),
                 make_item(3, 0, 0, {"techwear"}, {"L"})}, {});

    auto tagged = cat.query("techwear", "", 0, 10);
    REQUIRE(tagged.size() == 2);

    auto sized = cat.query("", "L", 0, 10);
    REQUIRE(sized.size() == 2);

    auto both = cat.query("techwear", "L", 0, 10);
    REQUIRE(both.size() == 1);
    CHECK(both[0]->id == 3);
}

TEST_CASE("catalog paginates", "[catalog]") {
    std::vector<Item> items;
    for (int i = 0; i < 10; ++i) items.push_back(make_item(i + 1, 0, 10 - i));
    Catalog cat(std::move(items), {});

    auto page = cat.query("", "", 3, 2);
    REQUIRE(page.size() == 2);
    CHECK(page[0]->id == 4);
    CHECK(page[1]->id == 5);
}

TEST_CASE("catalog collects the tag list for filter chips", "[catalog]") {
    Catalog cat({make_item(1, 0, 0, {"b", "a"}),
                 make_item(2, 0, 0, {"a", "c"})}, {});
    auto tags = cat.all_tags();
    REQUIRE(tags.size() == 3);
    CHECK(tags[0] == "a");   // sorted and de-duplicated
    CHECK(tags[1] == "b");
    CHECK(tags[2] == "c");
}

TEST_CASE("a held snapshot survives a swap", "[catalog]") {
    CatalogHandle handle;
    handle.set(std::make_shared<Catalog const>(
        std::vector<Item>{make_item(1, 0, 0)}, std::vector<Partner>{}));

    auto held = handle.get();            // a request fiber grabs the snapshot
    handle.set(std::make_shared<Catalog const>(
        std::vector<Item>{make_item(2, 0, 0)}, std::vector<Partner>{}));

    // The in-flight request still sees a consistent, complete old catalog.
    REQUIRE(held->size() == 1);
    CHECK(held->find(1) != nullptr);
    CHECK(handle.get()->find(2) != nullptr);
}
