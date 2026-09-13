#pragma once

#include "models.hpp"

#include <string>
#include <string_view>

namespace ssize {

// Percent-encodes everything outside the unreserved set (RFC 3986). Used for
// {url_enc}, which has to survive being a query parameter of the partner URL.
std::string url_encode(std::string_view in);

// Opaque click token used as the partner's `subid`. Deliberately NOT derived
// from anon_id: the token means nothing outside our database, so the affiliate
// network never receives anything that identifies the visitor.
// 12 chars of base62 ~= 71 bits, and it is generated per click.
std::string make_click_token();

struct LinkVars {
    std::string_view url;       // clean product URL
    std::string_view subid;     // click token
    std::string_view campaign;  // partner offer code
};

// Substitutes {url}, {url_enc}, {subid}, {campaign} in a partner template.
// Unknown placeholders are left untouched so a typo in the DB is visible in the
// resulting URL rather than silently dropping a parameter.
std::string render_link(std::string_view tmpl, LinkVars const& vars);

// Convenience: full outbound URL for an item.
std::string outbound_url(Item const& item,
                         Partner const& partner,
                         std::string_view click_token);

}  // namespace ssize
