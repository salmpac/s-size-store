#include "migrations.hpp"

#include "db.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ssize {

namespace {

struct Migration {
    int                   version;
    std::filesystem::path path;
};

// Expects filenames like "001_init.sql"; anything else is ignored so stray
// files in the directory cannot break a deploy.
std::vector<Migration> discover(std::filesystem::path const& dir) {
    std::vector<Migration> out;
    if (!std::filesystem::is_directory(dir)) {
        throw std::runtime_error("migrations dir not found: " + dir.string());
    }

    for (auto const& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sql") continue;

        std::string name = entry.path().filename().string();
        int version = 0;
        auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), version);
        if (ec != std::errc{} || version <= 0) {
            spdlog::warn("skipping unrecognised migration file '{}'", name);
            continue;
        }
        out.push_back(Migration{version, entry.path()});
    }

    std::sort(out.begin(), out.end(),
              [](Migration const& a, Migration const& b) { return a.version < b.version; });
    return out;
}

std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int apply_migrations(DbConn& conn, std::filesystem::path const& dir) {
    int current = conn.user_version();

    for (auto const& m : discover(dir)) {
        if (m.version <= current) continue;

        spdlog::info("applying migration {} ({})", m.version, m.path.filename().string());
        conn.exec("BEGIN;");
        try {
            conn.exec(read_file(m.path));
            conn.set_user_version(m.version);
            conn.exec("COMMIT;");
        } catch (...) {
            conn.exec("ROLLBACK;");
            throw;
        }
        current = m.version;
    }

    return current;
}

}  // namespace ssize
