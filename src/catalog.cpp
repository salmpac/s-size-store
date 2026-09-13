#include "catalog.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace ssize {

namespace {

TsMs now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

Catalog::Catalog(std::vector<Item> items, std::vector<Partner> partners)
    : items_(std::move(items)), partners_(std::move(partners)), built_at_(now_ms()) {

    // The DB already returns feed order; sorting here keeps the invariant true
    // even if a caller hands us rows in another order (tests, admin tooling).
    std::stable_sort(items_.begin(), items_.end(), [](Item const& a, Item const& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.published_at.value_or(0) > b.published_at.value_or(0);
    });

    by_id_.reserve(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        by_id_.emplace(items_[i].id, i);
    }

    partner_by_id_.reserve(partners_.size());
    for (std::size_t i = 0; i < partners_.size(); ++i) {
        partner_by_id_.emplace(partners_[i].id, i);
    }

    std::set<std::string> tags;
    for (auto const& item : items_) {
        tags.insert(item.tags.begin(), item.tags.end());
    }
    tags_.assign(tags.begin(), tags.end());
}

Item const* Catalog::find(std::int64_t id) const noexcept {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &items_[it->second];
}

Partner const* Catalog::partner(std::int64_t id) const noexcept {
    auto it = partner_by_id_.find(id);
    return it == partner_by_id_.end() ? nullptr : &partners_[it->second];
}

std::vector<Item const*> Catalog::query(std::string_view tag,
                                        std::string_view size,
                                        std::size_t offset,
                                        std::size_t limit) const {
    std::vector<Item const*> out;
    out.reserve(std::min<std::size_t>(limit, items_.size()));

    std::size_t skipped = 0;
    for (auto const& item : items_) {
        if (!tag.empty() &&
            std::find(item.tags.begin(), item.tags.end(), tag) == item.tags.end()) {
            continue;
        }
        if (!size.empty() &&
            std::find(item.sizes.begin(), item.sizes.end(), size) == item.sizes.end()) {
            continue;
        }
        if (skipped++ < offset) continue;
        out.push_back(&item);
        if (out.size() >= limit) break;
    }
    return out;
}

}  // namespace ssize
