#include "models.hpp"

namespace ssize {

std::string_view to_string(ItemStatus s) noexcept {
    switch (s) {
        case ItemStatus::Draft:     return "draft";
        case ItemStatus::Published: return "published";
        case ItemStatus::Archived:  return "archived";
    }
    return "draft";
}

std::optional<ItemStatus> item_status_from_string(std::string_view s) noexcept {
    if (s == "draft")     return ItemStatus::Draft;
    if (s == "published") return ItemStatus::Published;
    if (s == "archived")  return ItemStatus::Archived;
    return std::nullopt;
}

std::string_view to_string(EventType t) noexcept {
    switch (t) {
        case EventType::Impression: return "impression";
        case EventType::CardClick:  return "card_click";
        case EventType::Outbound:   return "outbound";
        case EventType::Search:     return "search";
    }
    return "impression";
}

}  // namespace ssize
