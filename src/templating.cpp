#include "templating.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace ssize {

Templating::Templating(std::filesystem::path dir)
    : dir_(std::move(dir)), env_((dir_.string() + "/")) {
    if (!std::filesystem::is_directory(dir_)) {
        throw std::runtime_error("templates dir not found: " + dir_.string());
    }

    env_.set_trim_blocks(true);
    env_.set_lstrip_blocks(true);

    // Parse everything up front so a typo in a template is a startup failure
    // rather than a 500 on the first visitor who hits that page.
    for (auto const& entry : std::filesystem::directory_iterator(dir_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".html") continue;

        auto name = entry.path().filename().string();
        try {
            cache_.emplace(name, env_.parse_template(name));
        } catch (std::exception const& e) {
            throw std::runtime_error("template '" + name + "' failed to parse: " + e.what());
        }
    }

    spdlog::info("loaded {} templates from {}", cache_.size(), dir_.string());
}

std::string Templating::render(std::string const& name, nlohmann::json const& data) {
    auto it = cache_.find(name);
    if (it == cache_.end()) {
        throw std::runtime_error("no such template: " + name);
    }
    try {
        return env_.render(it->second, data);
    } catch (std::exception const& e) {
        spdlog::error("template '{}' failed: {}", name, e.what());
        throw;
    }
}

}  // namespace ssize
