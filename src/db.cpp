#include "db.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ssize {

namespace {

[[noreturn]] void throw_sqlite(sqlite3* db, std::string_view what) {
    throw std::runtime_error(std::string(what) + ": " +
                             (db ? sqlite3_errmsg(db) : "no connection"));
}

}  // namespace

// ------------------------------------------------------------------- Stmt --

Stmt::Stmt(sqlite3* db, std::string_view sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()),
                           &stmt_, nullptr) != SQLITE_OK) {
        throw_sqlite(db, "prepare failed");
    }
}

Stmt::~Stmt() { sqlite3_finalize(stmt_); }

Stmt::Stmt(Stmt&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      stmt_(std::exchange(other.stmt_, nullptr)) {}

Stmt& Stmt::bind(int idx, std::int64_t v) {
    if (sqlite3_bind_int64(stmt_, idx, v) != SQLITE_OK) throw_sqlite(db_, "bind int");
    return *this;
}

Stmt& Stmt::bind(int idx, double v) {
    if (sqlite3_bind_double(stmt_, idx, v) != SQLITE_OK) throw_sqlite(db_, "bind real");
    return *this;
}

Stmt& Stmt::bind(int idx, std::string_view v) {
    if (sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw_sqlite(db_, "bind text");
    }
    return *this;
}

Stmt& Stmt::bind(int idx, std::span<std::uint8_t const> v) {
    if (v.empty()) return bind_null(idx);
    if (sqlite3_bind_blob(stmt_, idx, v.data(), static_cast<int>(v.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw_sqlite(db_, "bind blob");
    }
    return *this;
}

Stmt& Stmt::bind_null(int idx) {
    if (sqlite3_bind_null(stmt_, idx) != SQLITE_OK) throw_sqlite(db_, "bind null");
    return *this;
}

bool Stmt::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    throw_sqlite(db_, "step failed");
}

void Stmt::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::int64_t Stmt::column_int (int i) const { return sqlite3_column_int64(stmt_, i); }
double       Stmt::column_real(int i) const { return sqlite3_column_double(stmt_, i); }

std::string Stmt::column_text(int i) const {
    auto const* p = sqlite3_column_text(stmt_, i);
    if (!p) return {};
    return {reinterpret_cast<char const*>(p),
            static_cast<std::size_t>(sqlite3_column_bytes(stmt_, i))};
}

bool Stmt::column_is_null(int i) const {
    return sqlite3_column_type(stmt_, i) == SQLITE_NULL;
}

std::optional<std::int64_t> Stmt::column_opt_int(int i) const {
    if (column_is_null(i)) return std::nullopt;
    return column_int(i);
}

// ----------------------------------------------------------------- DbConn --

DbConn::DbConn(std::filesystem::path const& path) {
    if (sqlite3_open(path.c_str(), &conn_) != SQLITE_OK) {
        std::string msg = conn_ ? sqlite3_errmsg(conn_) : "out of memory";
        sqlite3_close(conn_);
        throw std::runtime_error("cannot open db " + path.string() + ": " + msg);
    }
    // WAL lets the reader build a snapshot while the writer appends events.
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA busy_timeout = 5000;");
}

DbConn::~DbConn() { sqlite3_close(conn_); }

void DbConn::exec(std::string_view sql) {
    char* err = nullptr;
    std::string owned(sql);
    if (sqlite3_exec(conn_, owned.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("exec failed: " + msg);
    }
}

int DbConn::user_version() {
    Stmt st(conn_, "PRAGMA user_version;");
    return st.step() ? static_cast<int>(st.column_int(0)) : 0;
}

void DbConn::set_user_version(int v) {
    exec("PRAGMA user_version = " + std::to_string(v) + ";");
}

// --- catalog reads ---

std::vector<Partner> DbConn::list_partners() {
    std::vector<Partner> out;
    Stmt st(conn_, "SELECT id, code, title, link_template, enabled FROM partners;");
    while (st.step()) {
        out.push_back(Partner{
            .id            = st.column_int(0),
            .code          = st.column_text(1),
            .title         = st.column_text(2),
            .link_template = st.column_text(3),
            .enabled       = st.column_int(4) != 0,
        });
    }
    return out;
}

std::vector<Item> DbConn::load_published_items() {
    std::vector<Item> items;
    std::unordered_map<std::int64_t, std::size_t> index;

    {
        Stmt st(conn_, R"(
            SELECT id, title, description, COALESCE(brand,''), price_kopek,
                   old_price_kopek, currency, COALESCE(partner_id,0), product_url,
                   COALESCE(campaign,''), status, score,
                   created_at, updated_at, published_at
            FROM items
            WHERE status = 'published'
            ORDER BY score DESC, published_at DESC;
        )");
        while (st.step()) {
            Item it;
            it.id              = st.column_int(0);
            it.title           = st.column_text(1);
            it.description     = st.column_text(2);
            it.brand           = st.column_text(3);
            it.price_kopek     = st.column_int(4);
            it.old_price_kopek = st.column_opt_int(5);
            it.currency        = st.column_text(6);
            it.partner_id      = st.column_int(7);
            it.product_url     = st.column_text(8);
            it.campaign        = st.column_text(9);
            it.status          = item_status_from_string(st.column_text(10))
                                     .value_or(ItemStatus::Draft);
            it.score           = st.column_real(11);
            it.created_at      = st.column_int(12);
            it.updated_at      = st.column_int(13);
            it.published_at    = st.column_opt_int(14);

            index.emplace(it.id, items.size());
            items.push_back(std::move(it));
        }
    }

    // Children are loaded with one query each rather than per item: a few
    // hundred rows total, and it keeps the refresh cheap enough to run every
    // 10 seconds without thinking about it.
    auto attach = [&](std::string_view sql, auto&& fn) {
        Stmt st(conn_, sql);
        while (st.step()) {
            auto it = index.find(st.column_int(0));
            if (it == index.end()) continue;  // draft/archived parent
            fn(items[it->second], st);
        }
    };

    attach(R"(SELECT item_id, id, path, COALESCE(thumb_path,''),
                     COALESCE(width,0), COALESCE(height,0), sort_order
              FROM item_images ORDER BY item_id, sort_order;)",
           [](Item& item, Stmt const& st) {
               item.images.push_back(Image{
                   .id         = st.column_int(1),
                   .path       = st.column_text(2),
                   .thumb_path = st.column_text(3),
                   .width      = static_cast<int>(st.column_int(4)),
                   .height     = static_cast<int>(st.column_int(5)),
                   .sort_order = static_cast<int>(st.column_int(6)),
               });
           });

    attach(R"(SELECT it.item_id, t.slug FROM item_tags it
              JOIN tags t ON t.id = it.tag_id ORDER BY it.item_id, t.slug;)",
           [](Item& item, Stmt const& st) { item.tags.push_back(st.column_text(1)); });

    attach("SELECT item_id, size FROM item_sizes ORDER BY item_id;",
           [](Item& item, Stmt const& st) { item.sizes.push_back(st.column_text(1)); });

    attach("SELECT item_id, color, COALESCE(hex,'') FROM item_colors ORDER BY item_id;",
           [](Item& item, Stmt const& st) {
               item.colors.push_back(Color{st.column_text(1), st.column_text(2)});
           });

    return items;
}

// --- catalog writes ---

std::int64_t DbConn::insert_item(Item const& item) {
    Stmt st(conn_, R"(
        INSERT INTO items (title, description, brand, price_kopek, old_price_kopek,
                           currency, partner_id, product_url, campaign, status,
                           score, source_channel, created_at, updated_at, published_at)
        VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,NULL,?12,?13,?14);
    )");
    st.bind(1, item.title)
      .bind(2, item.description)
      .bind(3, item.brand)
      .bind(4, item.price_kopek)
      .bind(5, item.old_price_kopek)
      .bind(6, item.currency)
      .bind(7, item.partner_id)
      .bind(8, item.product_url)
      .bind(9, item.campaign)
      .bind(10, to_string(item.status))
      .bind(11, item.score)
      .bind(12, item.created_at)
      .bind(13, item.updated_at)
      .bind(14, item.published_at);
    st.step();
    return sqlite3_last_insert_rowid(conn_);
}

void DbConn::set_item_status(std::int64_t id, ItemStatus status, TsMs now) {
    Stmt st(conn_, R"(
        UPDATE items
           SET status = ?2,
               updated_at = ?3,
               published_at = CASE WHEN ?2 = 'published' AND published_at IS NULL
                                   THEN ?3 ELSE published_at END
         WHERE id = ?1;
    )");
    st.bind(1, id).bind(2, to_string(status)).bind(3, now);
    st.step();
}

std::int64_t DbConn::upsert_tag(std::string_view slug, std::string_view title) {
    {
        Stmt sel(conn_, "SELECT id FROM tags WHERE slug = ?1;");
        sel.bind(1, slug);
        if (sel.step()) return sel.column_int(0);
    }
    Stmt ins(conn_, "INSERT INTO tags (slug, title) VALUES (?1, ?2);");
    ins.bind(1, slug).bind(2, title);
    ins.step();
    return sqlite3_last_insert_rowid(conn_);
}

void DbConn::attach_tag(std::int64_t item_id, std::int64_t tag_id) {
    Stmt st(conn_,
            "INSERT OR IGNORE INTO item_tags (item_id, tag_id) VALUES (?1, ?2);");
    st.bind(1, item_id).bind(2, tag_id);
    st.step();
}

void DbConn::add_image(std::int64_t item_id, Image const& img) {
    Stmt st(conn_, R"(
        INSERT INTO item_images (item_id, path, thumb_path, width, height, sort_order)
        VALUES (?1,?2,?3,?4,?5,?6);
    )");
    st.bind(1, item_id)
      .bind(2, img.path)
      .bind(3, img.thumb_path)
      .bind(4, static_cast<std::int64_t>(img.width))
      .bind(5, static_cast<std::int64_t>(img.height))
      .bind(6, static_cast<std::int64_t>(img.sort_order));
    st.step();
}

std::optional<Partner> DbConn::find_partner_by_code(std::string_view code) {
    Stmt st(conn_,
            "SELECT id, code, title, link_template, enabled FROM partners WHERE code = ?1;");
    st.bind(1, code);
    if (!st.step()) return std::nullopt;
    return Partner{
        .id            = st.column_int(0),
        .code          = st.column_text(1),
        .title         = st.column_text(2),
        .link_template = st.column_text(3),
        .enabled       = st.column_int(4) != 0,
    };
}

// --- metrics ---

void DbConn::insert_events(std::span<Event const> batch) {
    if (batch.empty()) return;

    exec("BEGIN IMMEDIATE;");
    try {
        Stmt st(conn_, R"(
            INSERT INTO events (ts, anon_id, session_id, type, item_id, position,
                                surface, click_token, source, ua_class, ip_hash, payload)
            VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);
        )");
        for (auto const& ev : batch) {
            st.bind(1, ev.ts)
              .bind(2, ev.anon_id)
              .bind(3, ev.session_id)
              .bind(4, to_string(ev.type))
              .bind(5, ev.item_id)
              .bind(6, ev.position ? std::optional<std::int64_t>(*ev.position)
                                   : std::nullopt)
              .bind(7, ev.surface);
            if (ev.click_token.empty()) st.bind_null(8);
            else                        st.bind(8, ev.click_token);
            st.bind(9, ev.source)
              .bind(10, ev.ua_class)
              .bind(11, std::span<std::uint8_t const>(ev.ip_hash))
              .bind(12, ev.payload);
            st.step();
            st.reset();
        }
        exec("COMMIT;");
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

bool DbConn::record_conversion(std::string_view click_token,
                               TsMs ts,
                               std::string_view order_ref,
                               std::optional<Kopek> amount,
                               std::string_view status,
                               std::string_view raw) {
    // Only accept a postback we can tie to a click we actually served.
    {
        Stmt check(conn_, "SELECT 1 FROM events WHERE click_token = ?1;");
        check.bind(1, click_token);
        if (!check.step()) return false;
    }

    Stmt st(conn_, R"(
        INSERT INTO conversions (click_token, ts, order_ref, amount_kopek, status, raw)
        VALUES (?1,?2,?3,?4,?5,?6)
        -- The unique index on order_ref is partial (it skips NULLs), so the
        -- conflict target has to repeat that WHERE clause or sqlite will not
        -- match it.
        ON CONFLICT(order_ref) WHERE order_ref IS NOT NULL DO UPDATE
            SET status = excluded.status,
                amount_kopek = excluded.amount_kopek,
                ts = excluded.ts;
    )");
    st.bind(1, click_token).bind(2, ts);
    if (order_ref.empty()) st.bind_null(3); else st.bind(3, order_ref);
    st.bind(4, amount).bind(5, status).bind(6, raw);
    st.step();
    return true;
}

void DbConn::rollup_daily(std::int64_t day_from, std::int64_t day_to) {
    Stmt st(conn_, R"(
        INSERT INTO daily_item_stats (day, item_id, impressions, card_clicks, outbounds)
        SELECT ts / 86400000 AS day,
               item_id,
               SUM(type = 'impression'),
               SUM(type = 'card_click'),
               SUM(type = 'outbound')
          FROM events
         WHERE item_id IS NOT NULL
           AND ua_class != 'bot'
           AND ts / 86400000 BETWEEN ?1 AND ?2
         GROUP BY day, item_id
        ON CONFLICT(day, item_id) DO UPDATE
            SET impressions = excluded.impressions,
                card_clicks = excluded.card_clicks,
                outbounds   = excluded.outbounds;
    )");
    st.bind(1, day_from).bind(2, day_to);
    st.step();
}

int DbConn::prune_events(TsMs older_than) {
    Stmt st(conn_, "DELETE FROM events WHERE ts < ?1;");
    st.bind(1, older_than);
    st.step();
    return sqlite3_changes(conn_);
}

}  // namespace ssize
