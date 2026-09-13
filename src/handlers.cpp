#include "handlers.hpp"

#include "config.hpp"
#include "links.hpp"

#include <sodium.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <sstream>

namespace ssize {

namespace http = boost::beast::http;

namespace {

TsMs now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string random_id() {
    std::array<std::uint8_t, 16> raw{};
    randombytes_buf(raw.data(), raw.size());
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (auto b : raw) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

HttpResponse make_response(http::status st, std::string body,
                           std::string_view content_type) {
    HttpResponse res{st, 11};
    res.set(http::field::content_type, content_type);
    res.body() = std::move(body);
    return res;
}

HttpResponse text_response(http::status st, std::string body) {
    return make_response(st, std::move(body), "text/plain; charset=utf-8");
}

std::optional<std::int64_t> parse_int(std::string_view s) {
    std::int64_t v{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

// Source of a visit: utm_source if present, else the referrer's host, else
// "direct". Kept coarse on purpose — a free-text referrer is unusable in a
// GROUP BY.
std::string visit_source(std::unordered_map<std::string, std::string> const& q,
                         std::string_view referer) {
    if (auto it = q.find("utm_source"); it != q.end() && !it->second.empty()) {
        return it->second;
    }
    if (referer.empty()) return "direct";

    auto scheme = referer.find("://");
    auto start  = scheme == std::string_view::npos ? 0 : scheme + 3;
    auto end    = referer.find('/', start);
    auto host   = referer.substr(start, end == std::string_view::npos
                                            ? std::string_view::npos
                                            : end - start);
    return std::string(host);
}

}  // namespace

// ---------------------------------------------------------------- helpers --

std::string url_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_query(std::string_view qs) {
    std::unordered_map<std::string, std::string> out;
    while (!qs.empty()) {
        auto amp  = qs.find('&');
        auto pair = qs.substr(0, amp);
        if (!pair.empty()) {
            auto eq = pair.find('=');
            if (eq == std::string_view::npos) {
                out.emplace(url_decode(pair), "");
            } else {
                out.insert_or_assign(url_decode(pair.substr(0, eq)),
                                     url_decode(pair.substr(eq + 1)));
            }
        }
        if (amp == std::string_view::npos) break;
        qs.remove_prefix(amp + 1);
    }
    return out;
}

std::string cookie_value(std::string_view header, std::string_view name) {
    while (!header.empty()) {
        while (!header.empty() && (header.front() == ' ' || header.front() == ';')) {
            header.remove_prefix(1);
        }
        auto semi = header.find(';');
        auto pair = header.substr(0, semi);
        auto eq   = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == name) {
            return std::string(pair.substr(eq + 1));
        }
        if (semi == std::string_view::npos) break;
        header.remove_prefix(semi + 1);
    }
    return {};
}

Target split_target(std::string_view target) {
    auto q = target.find('?');
    if (q == std::string_view::npos) return {std::string(target), {}};
    return {std::string(target.substr(0, q)), std::string(target.substr(q + 1))};
}

// ---------------------------------------------------------------- context --

HandlerContext::HandlerContext(Config const& c, CatalogHandle& cat, EventQueue& ev,
                               ConversionQueue& cv)
    : cfg(c), catalog(cat), events(ev), conversions(cv), tpl(c.templates_dir) {}

// --------------------------------------------------------------- dispatch --

namespace {

// Identity cookies. `aid` is the device (1 year), `sid` the visit (30 min idle
// — refreshed on every request). Neither is ever sent to a third party.
struct Identity {
    std::string anon_id;
    std::string session_id;
    bool        set_anon{false};
    bool        set_session{false};
};

Identity identify(HttpRequest const& req) {
    Identity id;
    auto cookies = req[http::field::cookie];
    id.anon_id    = cookie_value(cookies, "aid");
    id.session_id = cookie_value(cookies, "sid");

    if (id.anon_id.empty()) {
        id.anon_id  = random_id();
        id.set_anon = true;
    }
    if (id.session_id.empty()) {
        id.session_id  = random_id();
        id.set_session = true;
    }
    return id;
}

void apply_identity(HttpResponse& res, Identity const& id, Config const& cfg) {
    auto cookie = [&](std::string_view name, std::string const& value, long max_age) {
        std::ostringstream ss;
        ss << name << '=' << value
           << "; Path=" << (cfg.base_path.empty() ? "/" : cfg.base_path)
           << "; Max-Age=" << max_age
           << "; HttpOnly; SameSite=Lax";
        if (cfg.cookie_secure) ss << "; Secure";
        res.insert(http::field::set_cookie, ss.str());
    };

    if (id.set_anon) cookie("aid", id.anon_id, cfg.anon_cookie_ttl.count());
    // The session cookie is re-sent on every request so its idle window slides.
    cookie("sid", id.session_id, cfg.session_idle_ttl.count());
}

void log_event(HandlerContext& ctx, Identity const& id, HttpRequest const& req,
               std::string_view client_ip, Event ev) {
    ev.ts         = now_ms();
    ev.anon_id    = id.anon_id;
    ev.session_id = id.session_id;
    ev.ua_class   = classify_ua(req[http::field::user_agent]);
    ev.ip_hash    = hash_ip(client_ip, ctx.cfg.ip_hash_secret);
    if (!ctx.events.push(std::move(ev))) {
        spdlog::warn("event queue full, dropping event");
    }
}

nlohmann::json item_to_json(Item const& item, Config const& cfg, int position) {
    nlohmann::json j;
    j["id"]    = item.id;
    j["title"] = item.title;
    j["brand"] = item.brand;
    j["price"] = item.price_kopek / 100;
    if (item.old_price_kopek) j["old_price"] = *item.old_price_kopek / 100;
    j["tags"]  = item.tags;
    j["sizes"] = item.sizes;
    j["position"] = position;
    j["url"]   = cfg.base_path + "/item/" + std::to_string(item.id);
    j["out"]   = cfg.base_path + "/r/" + std::to_string(item.id) +
                 "?p=" + std::to_string(position);

    if (!item.images.empty()) {
        auto const& img = item.images.front();
        j["image"] = cfg.base_path + "/media/" +
                     (img.thumb_path.empty() ? img.path : img.thumb_path);
    } else {
        j["image"] = "";
    }

    nlohmann::json colors = nlohmann::json::array();
    for (auto const& c : item.colors) {
        colors.push_back({{"name", c.name}, {"hex", c.hex}});
    }
    j["colors"] = colors;
    return j;
}

// GET / — the feed. Impressions are NOT logged here: the server cannot know
// what actually entered the viewport, so the page reports them via /api/ev.
HttpResponse handle_feed(HandlerContext& ctx, Identity const& id,
                         std::unordered_map<std::string, std::string> const& q) {
    auto catalog = ctx.catalog.get();

    std::string tag  = q.count("tag")  ? q.at("tag")  : "";
    std::string size = q.count("size") ? q.at("size") : "";
    std::string query = q.count("q") ? q.at("q") : "";

    // A new snapshot invalidates everything rendered from the old one.
    if (ctx.feed_cache_for != catalog) {
        ctx.feed_cache.clear();
        ctx.feed_cache_for = catalog;
    }

    // Free-text search echoes back into the page, so those responses are not
    // shared. Everything else is keyed by the filter pair.
    bool cacheable = query.empty();
    std::string key = tag + "\x1f" + size;

    if (cacheable) {
        if (auto hit = ctx.feed_cache.find(key); hit != ctx.feed_cache.end()) {
            return make_response(http::status::ok, hit->second,
                                 "text/html; charset=utf-8");
        }
    }

    auto items = catalog->query(tag, size, 0, 120);

    nlohmann::json data;
    data["base"] = ctx.cfg.base_path;
    data["page_title"] = "s-size — отобранные вещи";
    data["query"] = query;
    data["metrica_counter"] = ctx.cfg.metrica_counter;
    data["active_tag"]  = tag;
    data["active_size"] = size;

    nlohmann::json arr = nlohmann::json::array();
    for (std::size_t i = 0; i < items.size(); ++i) {
        arr.push_back(item_to_json(*items[i], ctx.cfg, static_cast<int>(i)));
    }
    data["items"] = arr;
    data["total"] = catalog->size();

    nlohmann::json tags = nlohmann::json::array();
    for (auto const& t : catalog->all_tags()) tags.push_back(t);
    data["tags"] = tags;

    (void)id;
    std::string html = ctx.tpl.render("feed.html", data);

    // Bounded so an arbitrary ?tag= cannot grow the map without limit; past the
    // cap the page is simply rendered fresh.
    constexpr std::size_t kMaxCachedFeeds = 64;
    if (cacheable && ctx.feed_cache.size() < kMaxCachedFeeds) {
        ctx.feed_cache.emplace(key, html);
    }

    return make_response(http::status::ok, std::move(html),
                         "text/html; charset=utf-8");
}

// GET /item/{id}
HttpResponse handle_item(HandlerContext& ctx, Identity const& id,
                         HttpRequest const& req, std::string_view client_ip,
                         std::int64_t item_id,
                         std::unordered_map<std::string, std::string> const& q) {
    auto catalog = ctx.catalog.get();
    Item const* item = catalog->find(item_id);
    if (!item) return text_response(http::status::not_found, "not found");

    std::optional<int> position;
    if (auto it = q.find("p"); it != q.end()) {
        if (auto p = parse_int(it->second)) position = static_cast<int>(*p);
    }

    log_event(ctx, id, req, client_ip, Event{
        .type     = EventType::CardClick,
        .item_id  = item_id,
        .position = position,
        .surface  = q.count("s") ? q.at("s") : "feed",
        .source   = visit_source(q, req[http::field::referer]),
    });

    nlohmann::json data;
    data["base"] = ctx.cfg.base_path;
    data["page_title"] = item->title + " — s-size";
    data["query"] = "";
    data["metrica_counter"] = ctx.cfg.metrica_counter;
    data["item"] = item_to_json(*item, ctx.cfg, position.value_or(-1));
    data["item"]["description"] = item->description;

    nlohmann::json images = nlohmann::json::array();
    for (auto const& img : item->images) {
        images.push_back(ctx.cfg.base_path + "/media/" + img.path);
    }
    data["item"]["images"] = images;

    return make_response(http::status::ok, ctx.tpl.render("item.html", data),
                         "text/html; charset=utf-8");
}

// GET /r/{id} — the outbound redirect. This is the only way a visitor ever
// reaches a partner, which is what makes the click count exact and lets the
// partner be swapped without touching the catalog. See docs/analytics.md.
HttpResponse handle_redirect(HandlerContext& ctx, Identity const& id,
                             HttpRequest const& req, std::string_view client_ip,
                             std::int64_t item_id,
                             std::unordered_map<std::string, std::string> const& q) {
    auto catalog = ctx.catalog.get();
    Item const* item = catalog->find(item_id);
    if (!item) return text_response(http::status::not_found, "not found");

    Partner const* partner = catalog->partner(item->partner_id);
    if (!partner || !partner->enabled) {
        // Falling back to the raw URL keeps the visitor moving; the click is
        // still recorded, only the attribution is lost.
        spdlog::warn("item {} has no usable partner, sending raw url", item_id);
    }

    std::string token = make_click_token();

    std::optional<int> position;
    if (auto it = q.find("p"); it != q.end()) {
        if (auto p = parse_int(it->second)) position = static_cast<int>(*p);
    }

    log_event(ctx, id, req, client_ip, Event{
        .type        = EventType::Outbound,
        .item_id     = item_id,
        .position    = position,
        .surface     = q.count("s") ? q.at("s") : "feed",
        .click_token = token,
        .source      = visit_source(q, req[http::field::referer]),
    });

    std::string target = partner && partner->enabled
                             ? outbound_url(*item, *partner, token)
                             : item->product_url;

    HttpResponse res{http::status::found, 11};
    res.set(http::field::location, target);
    // 302, never 301: a permanent redirect is cached by the browser forever,
    // which would silently stop both the click log and any partner change.
    res.set(http::field::cache_control, "no-store");
    res.set("Referrer-Policy", "no-referrer");
    res.body() = "";
    return res;
}

// GET /static/* and /media/* — only used when running without a reverse proxy.
// In production nginx serves both directly; it does sendfile and caching far
// better than we would, and it keeps image bytes out of this process entirely.
HttpResponse handle_file(HttpRequest const& req,
                         std::filesystem::path const& root,
                         std::string_view rel) {
    // Reject anything that could climb out of the root before touching disk.
    if (rel.find("..") != std::string_view::npos) {
        return text_response(http::status::forbidden, "forbidden");
    }

    auto path = root / std::filesystem::path(std::string(rel)).relative_path();

    std::error_code ec;
    auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    auto canonical_path = std::filesystem::weakly_canonical(path, ec);
    if (ec || canonical_path.string().rfind(canonical_root.string(), 0) != 0) {
        return text_response(http::status::forbidden, "forbidden");
    }

    // Validator from mtime + size. An immutable long max-age would be right for
    // content-hashed filenames, but these paths are stable and their contents
    // change (a real photo replacing a placeholder, a CSS edit), so a plain
    // max-age serves a stale file for an hour with no way to force a refresh.
    // no-cache means "keep it, but ask first" — the revalidation is a 304.
    auto mtime = std::filesystem::last_write_time(canonical_path, ec);
    auto fsize = std::filesystem::file_size(canonical_path, ec);
    std::string etag;
    if (!ec) {
        etag = "\"" + std::to_string(mtime.time_since_epoch().count()) + "-" +
               std::to_string(fsize) + "\"";
        if (req[http::field::if_none_match] == etag) {
            HttpResponse nm{http::status::not_modified, 11};
            nm.set(http::field::etag, etag);
            nm.set(http::field::cache_control, "no-cache");
            return nm;
        }
    }

    std::ifstream in(canonical_path, std::ios::binary);
    if (!in) return text_response(http::status::not_found, "not found");

    std::ostringstream ss;
    ss << in.rdbuf();

    auto ext = canonical_path.extension().string();
    std::string_view mime = "application/octet-stream";
    if      (ext == ".css")  mime = "text/css; charset=utf-8";
    else if (ext == ".js")   mime = "application/javascript; charset=utf-8";
    else if (ext == ".svg")  mime = "image/svg+xml";
    else if (ext == ".webp") mime = "image/webp";
    else if (ext == ".avif") mime = "image/avif";
    else if (ext == ".png")  mime = "image/png";
    else if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";

    auto res = make_response(http::status::ok, ss.str(), mime);
    res.set(http::field::cache_control, "no-cache");
    if (!etag.empty()) res.set(http::field::etag, etag);
    return res;
}

// GET /cpa/postback — the affiliate network telling us a click turned into an
// order. Matching happens by click token, which is the only thing we ever gave
// the partner, so this is what closes the funnel from impression to revenue.
//
// Authenticated with a shared secret: without it anyone could invent
// conversions and poison every revenue number we have.
HttpResponse handle_postback(HandlerContext& ctx,
                             std::unordered_map<std::string, std::string> const& q) {
    if (ctx.cfg.postback_secret.empty()) {
        spdlog::error("postback received but no secret configured; refusing");
        return text_response(http::status::service_unavailable, "not configured");
    }

    auto sign = q.find("sign");
    if (sign == q.end() ||
        sign->second.size() != ctx.cfg.postback_secret.size() ||
        sodium_memcmp(sign->second.data(), ctx.cfg.postback_secret.data(),
                      ctx.cfg.postback_secret.size()) != 0) {
        // Constant-time compare: a timing oracle here would hand out the secret.
        spdlog::warn("postback with bad signature");
        return text_response(http::status::forbidden, "bad signature");
    }

    auto subid = q.find("subid");
    if (subid == q.end() || subid->second.empty()) {
        return text_response(http::status::bad_request, "missing subid");
    }

    Conversion conv;
    conv.click_token = subid->second;
    conv.ts          = now_ms();
    conv.order_ref   = q.count("order") ? q.at("order") : "";
    conv.status      = q.count("status") ? q.at("status") : "pending";
    if (auto it = q.find("amount"); it != q.end()) {
        // Partners quote rubles; we store kopecks.
        try {
            conv.amount = static_cast<Kopek>(std::stod(it->second) * 100.0);
        } catch (std::exception const&) {
            return text_response(http::status::bad_request, "bad amount");
        }
    }

    if (conv.status != "pending" && conv.status != "approved" && conv.status != "rejected") {
        return text_response(http::status::bad_request, "bad status");
    }

    ctx.conversions.push(std::move(conv));
    // Networks retry on anything but a 200, so acknowledge as soon as it is
    // queued rather than after it reaches disk.
    return text_response(http::status::ok, "ok");
}

// POST /api/ev — impression batches from the page, sent with sendBeacon.
HttpResponse handle_beacon(HandlerContext& ctx, Identity const& id,
                           HttpRequest const& req, std::string_view client_ip) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body());
    } catch (std::exception const&) {
        return text_response(http::status::bad_request, "bad json");
    }
    if (!body.is_array()) return text_response(http::status::bad_request, "expected array");

    // A page can only ever report a bounded number of impressions per flush;
    // anything beyond that is a script, not a viewport.
    constexpr std::size_t kMaxBatch = 200;
    std::size_t accepted = 0;

    for (auto const& e : body) {
        if (accepted >= kMaxBatch) break;
        if (!e.is_object()) continue;

        Event ev;
        ev.type = EventType::Impression;
        if (auto it = e.find("item"); it != e.end() && it->is_number_integer()) {
            ev.item_id = it->get<std::int64_t>();
        } else {
            continue;  // an impression without an item tells us nothing
        }
        if (auto it = e.find("p"); it != e.end() && it->is_number_integer()) {
            ev.position = it->get<int>();
        }
        if (auto it = e.find("s"); it != e.end() && it->is_string()) {
            ev.surface = it->get<std::string>();
        }
        log_event(ctx, id, req, client_ip, std::move(ev));
        ++accepted;
    }

    return text_response(http::status::no_content, "");
}

}  // namespace

HttpResponse dispatch(HttpRequest const& req, HandlerContext& ctx,
                      std::string_view client_ip) {
    HttpResponse res;
    Identity id = identify(req);

    try {
        auto [path, query] = split_target(req.target());
        auto q = parse_query(query);

        // Strip the deployment prefix so routes are written without it.
        std::string_view p = path;
        if (!ctx.cfg.base_path.empty() && p.starts_with(ctx.cfg.base_path)) {
            p.remove_prefix(ctx.cfg.base_path.size());
        }
        if (p.empty()) p = "/";

        auto method = req.method();

        if (method == http::verb::get && p == "/") {
            res = handle_feed(ctx, id, q);
        } else if (method == http::verb::get && p.starts_with("/item/")) {
            auto idv = parse_int(p.substr(6));
            res = idv ? handle_item(ctx, id, req, client_ip, *idv, q)
                      : text_response(http::status::bad_request, "bad id");
        } else if (method == http::verb::get && p.starts_with("/r/")) {
            auto idv = parse_int(p.substr(3));
            res = idv ? handle_redirect(ctx, id, req, client_ip, *idv, q)
                      : text_response(http::status::bad_request, "bad id");
        } else if (method == http::verb::get && p.starts_with("/static/")) {
            res = handle_file(req, ctx.cfg.static_dir, p.substr(8));
        } else if (method == http::verb::get && p.starts_with("/media/")) {
            res = handle_file(req, ctx.cfg.media_root, p.substr(7));
        } else if (method == http::verb::post && p == "/api/ev") {
            res = handle_beacon(ctx, id, req, client_ip);
        } else if (method == http::verb::get && p == "/cpa/postback") {
            res = handle_postback(ctx, q);
        } else if (method == http::verb::get && p == "/healthz") {
            res = text_response(http::status::ok, "ok");
        } else {
            res = text_response(http::status::not_found, "not found");
        }
    } catch (std::exception const& e) {
        spdlog::error("handler threw: {}", e.what());
        res = text_response(http::status::internal_server_error, "internal error");
    }

    apply_identity(res, id, ctx.cfg);
    res.set(http::field::server, "ssize");
    // Pages are rebuilt from a snapshot that changes every few seconds and they
    // carry identity cookies; nothing here is safe to hold onto.
    if (res.find(http::field::cache_control) == res.end()) {
        res.set(http::field::cache_control, "no-store");
    }
    return res;
}

}  // namespace ssize
