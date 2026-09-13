#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ssize {

// Money is kopecks everywhere. No floating point currency, ever.
using Kopek = std::int64_t;
// Unix milliseconds.
using TsMs = std::int64_t;

enum class ItemStatus { Draft, Published, Archived };

std::string_view to_string(ItemStatus s) noexcept;
std::optional<ItemStatus> item_status_from_string(std::string_view s) noexcept;

struct Image {
    std::int64_t id{};
    std::string  path;        // relative to media_root
    std::string  thumb_path;  // pre-generated feed preview; may be empty
    int          width{};
    int          height{};
    int          sort_order{};
};

struct Partner {
    std::int64_t id{};
    std::string  code;
    std::string  title;
    std::string  link_template;
    bool         enabled{true};
};

struct Color {
    std::string name;
    std::string hex;  // may be empty
};

struct Item {
    std::int64_t id{};
    std::string  title;
    std::string  description;
    std::string  brand;
    Kopek        price_kopek{};
    std::optional<Kopek> old_price_kopek;
    std::string  currency{"RUB"};

    std::int64_t partner_id{};
    std::string  product_url;
    std::string  campaign;

    ItemStatus   status{ItemStatus::Draft};
    double       score{};

    std::vector<Image>       images;
    std::vector<std::string> tags;   // slugs
    std::vector<std::string> sizes;
    std::vector<Color>       colors;

    TsMs created_at{};
    TsMs updated_at{};
    std::optional<TsMs> published_at;
};

// --- metrics ---

enum class EventType { Impression, CardClick, Outbound, Search };

std::string_view to_string(EventType t) noexcept;

struct Event {
    TsMs        ts{};
    std::string anon_id;
    std::string session_id;
    EventType   type{};
    std::optional<std::int64_t> item_id;
    // Position in the listing. Absent only for events with no listing context.
    // See docs/analytics.md: without it CTR is uninterpretable.
    std::optional<int> position;
    std::string surface;      // feed|search|related|item
    std::string click_token;  // outbound only
    std::string source;
    std::string ua_class;
    std::vector<std::uint8_t> ip_hash;
    std::string payload;      // JSON, may be empty
};

}  // namespace ssize
