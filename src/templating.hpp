#pragma once

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ssize {

// Templates are read and parsed once at startup, then rendered from memory.
//
// Parsing per request costs more than everything else in the response put
// together: the feed pulls in three files and walks the whole template, which
// held it to ~900 rps against ~8000 for a page half the size. Editing a
// template now needs a restart, which takes about as long as saving the file.
class Templating {
public:
    explicit Templating(std::filesystem::path dir);

    // Not const: inja mutates the environment while rendering. Only ever
    // called from the single HTTP thread.
    std::string render(std::string const& name, nlohmann::json const& data);

private:
    std::filesystem::path dir_;
    inja::Environment     env_;
    std::unordered_map<std::string, inja::Template> cache_;
};

}  // namespace ssize
