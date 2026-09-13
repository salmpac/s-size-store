#pragma once

#include "models.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ssize {

// An immutable view of the whole published catalog.
//
// The DB thread builds a fresh Catalog every refresh interval and swaps it in;
// readers (request fibers) grab a shared_ptr and work off it with no locking
// and no round trip through a queue. A snapshot stays alive as long as some
// request still holds it, so an in-flight request never sees a half-updated
// catalog.
class Catalog {
public:
    Catalog() = default;
    explicit Catalog(std::vector<Item> items, std::vector<Partner> partners);

    Catalog(Catalog const&) = delete;
    Catalog& operator=(Catalog const&) = delete;

    // Published items, already in feed order (score DESC, published_at DESC).
    std::span<Item const> feed() const noexcept { return items_; }

    Item const*    find(std::int64_t id) const noexcept;
    Partner const* partner(std::int64_t id) const noexcept;

    // Feed slice plus the tags/sizes present, for filter chips.
    std::vector<Item const*> query(std::string_view tag,
                                   std::string_view size,
                                   std::size_t offset,
                                   std::size_t limit) const;

    std::span<std::string const> all_tags() const noexcept { return tags_; }

    std::size_t size() const noexcept { return items_.size(); }
    TsMs built_at() const noexcept { return built_at_; }

private:
    std::vector<Item>    items_;
    std::vector<Partner> partners_;
    std::vector<std::string> tags_;

    std::unordered_map<std::int64_t, std::size_t> by_id_;
    std::unordered_map<std::int64_t, std::size_t> partner_by_id_;

    TsMs built_at_{};
};

using CatalogPtr = std::shared_ptr<Catalog const>;

// The single global handle readers load from. Swapped wholesale by the DB
// thread; never mutated in place.
class CatalogHandle {
public:
    CatalogPtr get() const noexcept { return std::atomic_load(&ptr_); }
    void set(CatalogPtr next) noexcept { std::atomic_store(&ptr_, std::move(next)); }

private:
    CatalogPtr ptr_{std::make_shared<Catalog const>()};
};

}  // namespace ssize
