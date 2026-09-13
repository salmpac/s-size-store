// Admin tooling: migrations, seeding, publishing, and a look at the funnel.
// Runs against the same database as the server; safe to use while it is up
// (WAL allows a second writer as long as transactions stay short).

#include "config.hpp"
#include "db.hpp"
#include "migrations.hpp"
#include "models.hpp"

#include <boost/program_options.hpp>
#include <sodium.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace po = boost::program_options;
using namespace ssize;

namespace {

TsMs now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int cmd_migrate(Config const& cfg) {
    DbConn conn(cfg.db_path);
    int v = apply_migrations(conn, cfg.migrations_dir);
    std::cout << "schema version: " << v << "\n";
    return 0;
}

// Generates a plausible catalog so the frontend can be built and the funnel
// exercised before any real content exists.
int cmd_seed(Config const& cfg, int count) {
    DbConn conn(cfg.db_path);

    auto partner = conn.find_partner_by_code("direct");
    if (!partner) {
        std::cerr << "no 'direct' partner; run migrate first\n";
        return 1;
    }

    static char const* kTitles[] = {
        "Оверсайз худи", "Карго-брюки", "Тактическая куртка", "Лонгслив с принтом",
        "Широкие джинсы", "Бомбер", "Шорты нейлон", "Флиска", "Жилет утилитарный",
        "Свитер крупной вязки", "Балаклава", "Рубашка оверсайз",
    };
    static char const* kBrands[] = {
        "Noname", "Rick Owens style", "Y2K", "Techwear", "Vintage", "Local",
    };
    static char const* kTags[] = {
        "techwear", "y2k", "gorpcore", "oversize", "vintage", "street", "grunge",
    };
    static char const* kSizes[] = {"XS", "S", "M", "L", "XL"};
    struct ColorDef { char const* name; char const* hex; };
    static ColorDef kColors[] = {
        {"чёрный", "#101014"}, {"графит", "#2a2a33"}, {"фиолетовый", "#6d4aff"},
        {"хаки", "#5c5f4a"},   {"белый", "#f2f2f5"},
    };

    std::mt19937 rng(42);  // fixed seed: reruns produce the same catalog
    auto pick = [&](auto const& arr) -> auto const& {
        std::uniform_int_distribution<std::size_t> d(0, std::size(arr) - 1);
        return arr[d(rng)];
    };

    auto const now = now_ms();
    conn.exec("BEGIN;");
    for (int i = 0; i < count; ++i) {
        std::uniform_int_distribution<int> price_d(1500, 25000);
        int price = price_d(rng);

        Item item;
        item.title       = std::string(pick(kTitles)) + " #" + std::to_string(i + 1);
        item.description = "Позиция из демо-каталога. Заменится реальным описанием.";
        item.brand       = pick(kBrands);
        item.price_kopek = price * 100;
        if (i % 3 == 0) item.old_price_kopek = (price + price / 3) * 100;
        item.partner_id  = partner->id;
        item.product_url = "https://example.com/product/" + std::to_string(i + 1);
        item.status      = ItemStatus::Published;
        std::uniform_real_distribution<double> score_d(0.0, 10.0);
        item.score       = score_d(rng);
        item.created_at  = now;
        item.updated_at  = now;
        item.published_at = now;

        auto id = conn.insert_item(item);

        std::uniform_int_distribution<int> ntags(1, 3);
        for (int t = 0, n = ntags(rng); t < n; ++t) {
            char const* slug = pick(kTags);
            conn.attach_tag(id, conn.upsert_tag(slug, slug));
        }

        {
            Stmt st(conn.raw(), "INSERT OR IGNORE INTO item_sizes (item_id, size) VALUES (?1,?2);");
            std::uniform_int_distribution<int> nsizes(2, 4);
            for (int s = 0, n = nsizes(rng); s < n; ++s) {
                st.bind(1, id).bind(2, std::string_view(pick(kSizes)));
                st.step();
                st.reset();
            }
        }
        {
            Stmt st(conn.raw(),
                    "INSERT OR IGNORE INTO item_colors (item_id, color, hex) VALUES (?1,?2,?3);");
            auto const& c = pick(kColors);
            st.bind(1, id).bind(2, std::string_view(c.name)).bind(3, std::string_view(c.hex));
            st.step();
        }

        // No real files yet; the template falls back to a placeholder tile.
        Image img;
        img.path       = "placeholder/" + std::to_string((i % 12) + 1) + ".svg";
        img.thumb_path = img.path;
        img.width = 600; img.height = 800; img.sort_order = 0;
        conn.add_image(id, img);
    }
    conn.exec("COMMIT;");

    std::cout << "seeded " << count << " published items\n";
    return 0;
}

int cmd_stats(Config const& cfg, int days) {
    DbConn conn(cfg.db_path);
    auto since = now_ms() - static_cast<TsMs>(days) * 86'400'000;

    std::cout << "--- funnel, last " << days << " days (bots excluded) ---\n";
    {
        Stmt st(conn.raw(), R"(
            SELECT type, COUNT(*), COUNT(DISTINCT anon_id)
              FROM events
             WHERE ts >= ?1 AND ua_class != 'bot'
             GROUP BY type;
        )");
        st.bind(1, since);
        while (st.step()) {
            std::cout << "  " << st.column_text(0) << ": " << st.column_int(1)
                      << " events, " << st.column_int(2) << " users\n";
        }
    }

    std::cout << "--- top items by outbound CTR (>=20 impressions) ---\n";
    {
        Stmt st(conn.raw(), R"(
            SELECT i.id, i.title,
                   SUM(e.type = 'impression') AS imp,
                   SUM(e.type = 'outbound')   AS out
              FROM events e JOIN items i ON i.id = e.item_id
             WHERE e.ts >= ?1 AND e.ua_class != 'bot'
             GROUP BY i.id
            HAVING imp >= 20
             ORDER BY CAST(out AS REAL) / imp DESC
             LIMIT 10;
        )");
        st.bind(1, since);
        while (st.step()) {
            auto imp = st.column_int(2), out = st.column_int(3);
            std::cout << "  [" << st.column_int(0) << "] " << st.column_text(1)
                      << " — " << out << "/" << imp << " = "
                      << (imp ? 100.0 * static_cast<double>(out) / static_cast<double>(imp) : 0.0)
                      << "%\n";
        }
    }

    std::cout << "--- conversions ---\n";
    {
        Stmt st(conn.raw(), R"(
            SELECT status, COUNT(*), COALESCE(SUM(amount_kopek),0)
              FROM conversions WHERE ts >= ?1 GROUP BY status;
        )");
        st.bind(1, since);
        while (st.step()) {
            std::cout << "  " << st.column_text(0) << ": " << st.column_int(1)
                      << " orders, " << st.column_int(2) / 100 << " RUB\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string command;
    int count = 60;
    int days  = 7;

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "print help")
        ("config,c", po::value<std::string>(&config_path)->required(), "TOML config")
        ("command", po::value<std::string>(&command)->required(),
         "migrate | seed | stats")
        ("count", po::value<int>(&count), "number of items for seed")
        ("days",  po::value<int>(&days),  "window for stats");

    po::positional_options_description pos;
    pos.add("command", 1);

    try {
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).positional(pos).run(), vm);
        if (vm.count("help")) {
            std::cout << "usage: ssize-admin <migrate|seed|stats> --config <path>\n\n"
                      << desc << std::endl;
            return 0;
        }
        po::notify(vm);
    } catch (std::exception const& e) {
        std::cerr << "error: " << e.what() << "\n\n" << desc << std::endl;
        return 2;
    }

    spdlog::set_pattern("[%^%l%$] %v");

    try {
        if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
        auto cfg = load_config(config_path);

        if (command == "migrate") return cmd_migrate(cfg);
        if (command == "seed")    return cmd_seed(cfg, count);
        if (command == "stats")   return cmd_stats(cfg, days);

        std::cerr << "unknown command: " << command << "\n";
        return 2;
    } catch (std::exception const& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }
}
