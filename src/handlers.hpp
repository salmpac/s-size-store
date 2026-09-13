#pragma once

#include "catalog.hpp"
#include "events.hpp"
#include "templating.hpp"

#include <boost/beast/http.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace ssize {

struct Config;

using HttpRequest  = boost::beast::http::request<boost::beast::http::string_body>;
using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

struct HandlerContext {
    Config const&    cfg;
    CatalogHandle&   catalog;
    EventQueue&      events;
    ConversionQueue& conversions;
    Templating       tpl;

    HandlerContext(Config const& c, CatalogHandle& cat, EventQueue& ev,
                   ConversionQueue& cv);
};

// Always returns a response; never throws.
HttpResponse dispatch(HttpRequest const& req, HandlerContext& ctx,
                      std::string_view client_ip);

// --- small helpers, exposed for tests ---

// Splits "a=1&b=2" into a map, percent-decoding both sides.
std::unordered_map<std::string, std::string> parse_query(std::string_view qs);
std::string url_decode(std::string_view in);

// Reads one cookie out of a Cookie header.
std::string cookie_value(std::string_view header, std::string_view name);

// Splits a target into path and query string.
struct Target { std::string path; std::string query; };
Target split_target(std::string_view target);

}  // namespace ssize
