#pragma once

#include "models.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ssize {

// RAII wrapper around a prepared statement.
class Stmt {
public:
    Stmt(sqlite3* db, std::string_view sql);
    ~Stmt();

    Stmt(Stmt const&) = delete;
    Stmt& operator=(Stmt const&) = delete;
    Stmt(Stmt&& other) noexcept;

    Stmt& bind(int idx, std::int64_t v);
    Stmt& bind(int idx, double v);
    Stmt& bind(int idx, std::string_view v);
    Stmt& bind(int idx, std::span<std::uint8_t const> v);
    Stmt& bind_null(int idx);

    template <typename T>
    Stmt& bind(int idx, std::optional<T> const& v) {
        return v ? bind(idx, *v) : bind_null(idx);
    }

    // Returns true while a row is available.
    bool step();
    void reset();

    std::int64_t column_int (int i) const;
    double       column_real(int i) const;
    std::string  column_text(int i) const;
    bool         column_is_null(int i) const;
    std::optional<std::int64_t> column_opt_int(int i) const;

    sqlite3_stmt* raw() const noexcept { return stmt_; }

private:
    sqlite3*      db_{};
    sqlite3_stmt* stmt_{};
};

// A single sqlite connection. Not thread-safe: owned either by the DB thread or
// by the admin CLI on the main thread, never shared.
class DbConn {
public:
    explicit DbConn(std::filesystem::path const& path);
    ~DbConn();

    DbConn(DbConn const&) = delete;
    DbConn& operator=(DbConn const&) = delete;

    sqlite3* raw() const noexcept { return conn_; }

    void exec(std::string_view sql);
    int  user_version();
    void set_user_version(int v);

    // --- catalog reads (used to build a Catalog snapshot) ---
    std::vector<Partner> list_partners();
    // Published items with images, tags, sizes and colors filled in.
    std::vector<Item>    load_published_items();

    // --- catalog writes (admin tooling) ---
    std::int64_t insert_item(Item const& item);
    void set_item_status(std::int64_t id, ItemStatus status, TsMs now);
    std::int64_t upsert_tag(std::string_view slug, std::string_view title);
    void attach_tag(std::int64_t item_id, std::int64_t tag_id);
    void add_image(std::int64_t item_id, Image const& img);
    std::optional<Partner> find_partner_by_code(std::string_view code);

    // --- metrics ---
    // Writes the whole batch inside one transaction. See docs/analytics.md.
    void insert_events(std::span<Event const> batch);
    // Matches an affiliate postback to the outbound click that produced it.
    bool record_conversion(std::string_view click_token,
                           TsMs ts,
                           std::string_view order_ref,
                           std::optional<Kopek> amount,
                           std::string_view status,
                           std::string_view raw);
    // Folds raw events into daily_item_stats for the given day range.
    void rollup_daily(std::int64_t day_from, std::int64_t day_to);
    // Drops raw events older than the retention window (90 days by default).
    int prune_events(TsMs older_than);

private:
    sqlite3* conn_{};
};

}  // namespace ssize
