#include "templating.hpp"

#include <inja/inja.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace ssize {

Templating::Templating(std::filesystem::path dir) : dir_(std::move(dir)) {
    if (!std::filesystem::is_directory(dir_)) {
        throw std::runtime_error("templates dir not found: " + dir_.string());
    }
}

std::string Templating::render(std::string const& name, nlohmann::json const& data) const {
    inja::Environment env((dir_.string() + "/"));
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);
    try {
        return env.render_file(name, data);
    } catch (std::exception const& e) {
        spdlog::error("template '{}' failed: {}", name, e.what());
        throw;
    }
}

}  // namespace ssize
