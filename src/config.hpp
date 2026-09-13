#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ssize {

struct Config {
    // --- http ---
    std::string   bind_address{"127.0.0.1"};
    std::uint16_t port{8080};
    int           io_threads{1};
    // Everything is served under this prefix while the MVP lives at
    // salmpac.ru/s-size; empty once s-size.ru is in place.
    std::string   base_path{""};

    // --- storage ---
    std::filesystem::path db_path{"ssize.db"};
    std::filesystem::path migrations_dir{"migrations"};
    std::filesystem::path templates_dir{"templates"};
    std::filesystem::path static_dir{"static"};
    std::filesystem::path media_root{"media"};

    // --- catalog snapshot ---
    std::chrono::seconds refresh_interval{10};

    // --- metrics ---
    std::chrono::milliseconds event_flush_interval{1000};
    std::size_t   event_queue_capacity{100'000};
    int           event_retention_days{90};
    // Salt for hash_ip(). Must be set in production; a fixed default would make
    // the hashes reversible by anyone with the source.
    std::string   ip_hash_secret;
    // Shared secret an affiliate postback must present.
    std::string   postback_secret;
    // Yandex Metrica counter id; the tag is omitted when this is 0.
    std::uint64_t metrica_counter{0};

    // --- cookies ---
    std::chrono::seconds anon_cookie_ttl{std::chrono::hours(24 * 365)};
    std::chrono::seconds session_idle_ttl{std::chrono::minutes(30)};
    bool cookie_secure{true};
};

// Throws std::runtime_error on a missing file or an invalid value.
Config load_config(std::filesystem::path const& path);

}  // namespace ssize
