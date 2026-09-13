#pragma once

#include <filesystem>

namespace ssize {

class DbConn;

// Applies every NNN_*.sql in `dir` whose number is above the database's
// user_version, in order, each inside its own transaction. Returns the schema
// version after applying. Idempotent: running it twice is a no-op.
int apply_migrations(DbConn& conn, std::filesystem::path const& dir);

}  // namespace ssize
