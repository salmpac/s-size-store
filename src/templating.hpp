#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ssize {

// Thin wrapper over inja. Templates are re-read from disk on every render in
// debug builds so the frontend can be iterated on without restarting; in
// release they are loaded once at startup.
class Templating {
public:
    explicit Templating(std::filesystem::path dir);

    std::string render(std::string const& name, nlohmann::json const& data) const;

private:
    std::filesystem::path dir_;
};

}  // namespace ssize
