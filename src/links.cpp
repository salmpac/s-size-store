#include "links.hpp"

#include <sodium.h>

#include <array>
#include <cstddef>

namespace ssize {

namespace {

constexpr std::string_view kBase62 =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

constexpr bool is_unreserved(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

constexpr char hex_digit(unsigned v) noexcept {
    return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10));
}

}  // namespace

std::string url_encode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char ch : in) {
        auto c = static_cast<unsigned char>(ch);
        if (is_unreserved(c)) {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(hex_digit(c >> 4u));
            out.push_back(hex_digit(c & 0x0Fu));
        }
    }
    return out;
}

std::string make_click_token() {
    constexpr std::size_t kLen = 12;
    std::array<std::uint8_t, kLen> raw{};
    randombytes_buf(raw.data(), raw.size());

    std::string out;
    out.reserve(kLen);
    for (std::uint8_t b : raw) {
        // Modulo bias over 62 of a uniform byte is negligible here: the token is
        // an opaque identifier, not a secret that guards anything by itself.
        out.push_back(kBase62[b % kBase62.size()]);
    }
    return out;
}

std::string render_link(std::string_view tmpl, LinkVars const& vars) {
    std::string out;
    out.reserve(tmpl.size() + vars.url.size() * 3);

    for (std::size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] != '{') {
            out.push_back(tmpl[i++]);
            continue;
        }

        auto close = tmpl.find('}', i);
        if (close == std::string_view::npos) {  // unterminated: copy verbatim
            out.append(tmpl.substr(i));
            break;
        }

        std::string_view name = tmpl.substr(i + 1, close - i - 1);
        if (name == "url") {
            out.append(vars.url);
        } else if (name == "url_enc") {
            out.append(url_encode(vars.url));
        } else if (name == "subid") {
            out.append(vars.subid);
        } else if (name == "campaign") {
            out.append(vars.campaign);
        } else {
            // Unknown placeholder: leave it in place so the mistake is visible.
            out.append(tmpl.substr(i, close - i + 1));
        }
        i = close + 1;
    }
    return out;
}

std::string outbound_url(Item const& item,
                         Partner const& partner,
                         std::string_view click_token) {
    return render_link(partner.link_template, LinkVars{
        .url      = item.product_url,
        .subid    = click_token,
        .campaign = item.campaign,
    });
}

}  // namespace ssize
