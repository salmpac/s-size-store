-- s-size initial schema.
-- Applied by src/migrations.cpp; user_version is bumped to 1.

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------- catalog --

-- Where an item's outbound link points. Changing partner or adding a referral
-- program is a single row edit here; the catalog is never rewritten.
CREATE TABLE partners (
    id            INTEGER PRIMARY KEY,
    code          TEXT    NOT NULL UNIQUE,   -- 'direct', 'admitad_ozon', ...
    title         TEXT    NOT NULL,
    -- Placeholders: {url} {url_enc} {subid} {campaign}
    link_template TEXT    NOT NULL,
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    INTEGER NOT NULL
);

CREATE TABLE items (
    id            INTEGER PRIMARY KEY,
    title         TEXT    NOT NULL,
    description   TEXT    NOT NULL DEFAULT '',
    brand         TEXT,
    -- Kopecks, not rubles: no floating point money anywhere.
    price_kopek   INTEGER NOT NULL,
    old_price_kopek INTEGER,               -- for a struck-through "was" price
    currency      TEXT    NOT NULL DEFAULT 'RUB',

    partner_id    INTEGER REFERENCES partners(id),
    product_url   TEXT    NOT NULL,        -- clean URL, never shown to the client
    campaign      TEXT,                    -- partner offer/campaign code

    -- draft: imported, not reviewed. published: visible. archived: hidden.
    status        TEXT    NOT NULL DEFAULT 'draft'
                  CHECK (status IN ('draft','published','archived')),
    -- Manual ranking nudge; ordering is score DESC, then published_at DESC.
    score         REAL    NOT NULL DEFAULT 0,

    source_channel TEXT,                   -- telegram channel it was lifted from
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL,
    published_at  INTEGER
);

CREATE INDEX idx_items_feed ON items(status, score DESC, published_at DESC);

-- Image files live on disk; the DB only stores relative paths under media_root.
CREATE TABLE item_images (
    id          INTEGER PRIMARY KEY,
    item_id     INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    path        TEXT    NOT NULL,          -- relative: 'ab/cd/8123_0.webp'
    thumb_path  TEXT,                      -- pre-generated feed-sized preview
    width       INTEGER,
    height      INTEGER,
    sort_order  INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_item_images_item ON item_images(item_id, sort_order);

-- Tags, sizes and colours are separate tables rather than JSON blobs: filtering
-- happens in RAM off the snapshot, but stats queries group by them in SQL.
CREATE TABLE tags (
    id    INTEGER PRIMARY KEY,
    slug  TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL
);

CREATE TABLE item_tags (
    item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    tag_id  INTEGER NOT NULL REFERENCES tags(id)  ON DELETE CASCADE,
    PRIMARY KEY (item_id, tag_id)
);

CREATE INDEX idx_item_tags_tag ON item_tags(tag_id);

CREATE TABLE item_sizes (
    item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    size    TEXT    NOT NULL,              -- 'S', 'M', '42', 'onesize'
    PRIMARY KEY (item_id, size)
);

CREATE TABLE item_colors (
    item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    color   TEXT    NOT NULL,              -- human label: 'чёрный'
    hex     TEXT,                          -- '#101014', for swatches
    PRIMARY KEY (item_id, color)
);

-- ---------------------------------------------------------------- metrics --

-- Append-only event log. Every number on /admin/stats is a query over this.
-- See docs/analytics.md.
CREATE TABLE events (
    id          INTEGER PRIMARY KEY,
    ts          INTEGER NOT NULL,          -- unix millis
    anon_id     TEXT    NOT NULL,          -- cookie 'aid', device, 1 year
    session_id  TEXT    NOT NULL,          -- cookie 'sid', 30 min idle
    type        TEXT    NOT NULL,          -- impression|card_click|outbound|search
    item_id     INTEGER,
    -- Position in the listing. Without it a high CTR is indistinguishable from
    -- "it was simply at the top". Never make it nullable by accident.
    position    INTEGER,
    surface     TEXT,                      -- feed|search|related|item
    click_token TEXT,                      -- outbound only; subid sent to partner
    source      TEXT,                      -- utm_source, else referrer host
    ua_class    TEXT,                      -- mobile|desktop|bot
    ip_hash     BLOB,                      -- hmac(ip, secret), never the raw IP
    payload     TEXT                       -- JSON escape hatch
);

CREATE INDEX idx_events_ts        ON events(ts);
CREATE INDEX idx_events_item_type ON events(item_id, type, ts);
CREATE INDEX idx_events_anon      ON events(anon_id, ts);
CREATE UNIQUE INDEX idx_events_token
    ON events(click_token) WHERE click_token IS NOT NULL;

-- Postbacks from the affiliate network, matched to an outbound click by token.
-- This is the only way the funnel reaches actual revenue.
CREATE TABLE conversions (
    id           INTEGER PRIMARY KEY,
    click_token  TEXT    NOT NULL,
    ts           INTEGER NOT NULL,
    order_ref    TEXT,
    amount_kopek INTEGER,
    status       TEXT    NOT NULL DEFAULT 'pending'
                 CHECK (status IN ('pending','approved','rejected')),
    raw          TEXT                      -- full query string, for disputes
);

CREATE INDEX idx_conversions_token ON conversions(click_token);
CREATE UNIQUE INDEX idx_conversions_order
    ON conversions(order_ref) WHERE order_ref IS NOT NULL;

-- Daily rollup. Raw events are pruned after 90 days; this is kept forever and
-- is what /admin/stats reads, so the dashboard never scans the event log.
CREATE TABLE daily_item_stats (
    day         INTEGER NOT NULL,          -- unix day number (ts/86400000)
    item_id     INTEGER NOT NULL,
    impressions INTEGER NOT NULL DEFAULT 0,
    card_clicks INTEGER NOT NULL DEFAULT 0,
    outbounds   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day, item_id)
);

-- ------------------------------------------------------------------ seed --

INSERT INTO partners (code, title, link_template, enabled, created_at)
VALUES ('direct', 'Прямая ссылка', '{url}', 1, 0);
