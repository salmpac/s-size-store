#include "config.hpp"

#include <toml++/toml.hpp>

#include <stdexcept>

namespace ssize {

namespace {

template <typename T>
T get_or(toml::table const& tbl, std::string_view key, T fallback) {
    if (auto node = tbl[key]; node) {
        if (auto v = node.value<T>()) return *v;
        throw std::runtime_error("config: bad type for key '" + std::string(key) + "'");
    }
    return fallback;
}

}  // namespace

Config load_config(std::filesystem::path const& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (toml::parse_error const& e) {
        throw std::runtime_error("config: cannot parse " + path.string() + ": " +
                                 std::string(e.description()));
    }

    Config cfg;

    if (auto http = tbl["http"].as_table()) {
        cfg.bind_address = get_or<std::string>(*http, "bind_address", cfg.bind_address);
        cfg.port      = static_cast<std::uint16_t>(get_or<std::int64_t>(*http, "port", cfg.port));
        cfg.io_threads = static_cast<int>(get_or<std::int64_t>(*http, "io_threads", cfg.io_threads));
        cfg.base_path = get_or<std::string>(*http, "base_path", cfg.base_path);
    }

    if (auto st = tbl["storage"].as_table()) {
        cfg.db_path        = get_or<std::string>(*st, "db_path", cfg.db_path.string());
        cfg.migrations_dir = get_or<std::string>(*st, "migrations_dir", cfg.migrations_dir.string());
        cfg.templates_dir  = get_or<std::string>(*st, "templates_dir", cfg.templates_dir.string());
        cfg.static_dir     = get_or<std::string>(*st, "static_dir", cfg.static_dir.string());
        cfg.media_root     = get_or<std::string>(*st, "media_root", cfg.media_root.string());
    }

    if (auto cat = tbl["catalog"].as_table()) {
        cfg.refresh_interval =
            std::chrono::seconds(get_or<std::int64_t>(*cat, "refresh_interval_sec",
                                                      cfg.refresh_interval.count()));
    }

    if (auto m = tbl["metrics"].as_table()) {
        cfg.event_flush_interval = std::chrono::milliseconds(
            get_or<std::int64_t>(*m, "flush_interval_ms", cfg.event_flush_interval.count()));
        cfg.event_queue_capacity = static_cast<std::size_t>(
            get_or<std::int64_t>(*m, "queue_capacity",
                                 static_cast<std::int64_t>(cfg.event_queue_capacity)));
        cfg.event_retention_days = static_cast<int>(
            get_or<std::int64_t>(*m, "retention_days", cfg.event_retention_days));
        cfg.ip_hash_secret  = get_or<std::string>(*m, "ip_hash_secret", "");
        cfg.postback_secret = get_or<std::string>(*m, "postback_secret", "");
        cfg.metrica_counter = static_cast<std::uint64_t>(
            get_or<std::int64_t>(*m, "metrica_counter", 0));
    }

    if (auto c = tbl["cookies"].as_table()) {
        cfg.cookie_secure = get_or<bool>(*c, "secure", cfg.cookie_secure);
        cfg.session_idle_ttl = std::chrono::seconds(
            get_or<std::int64_t>(*c, "session_idle_sec", cfg.session_idle_ttl.count()));
    }

    // Refusing to start beats silently hashing every visitor's IP with a known
    // salt, which would make the hashes trivially reversible.
    if (cfg.ip_hash_secret.empty()) {
        throw std::runtime_error("config: metrics.ip_hash_secret must be set");
    }

    return cfg;
}

}  // namespace ssize
