# s-size

Curated clothing catalog. C++ backend, server-rendered frontend, SQLite.

Concept and decisions: `project_concept.md`.
Metrics design: `docs/analytics.md`.

## Build

Everything comes from the flake — nothing is installed system-wide, so the
toolchain is identical here, in CI and on the server.

```sh
nix develop                      # dev shell with compiler, deps and tools
cmake --preset dev
cmake --build build/dev
ctest --preset dev
```

Or build the package directly:

```sh
nix build            # -> ./result/bin/ssize
```

## Run locally

```sh
cp config.example.toml config.toml
# set metrics.ip_hash_secret — the server refuses to start without it
sed -i "s|ip_hash_secret    = \"CHANGE_ME\"|ip_hash_secret    = \"$(head -c 32 /dev/urandom | base64)\"|" config.toml

./build/dev/ssize-admin migrate --config config.toml
./build/dev/ssize-admin seed    --config config.toml --count 60
./scripts/make-placeholders.sh          # tiles the seeded items point at
./build/dev/ssize --config config.toml
```

Then open <http://127.0.0.1:8080/s-size/>.

## Architecture

Two threads, no lock on the read path:

- **DB thread** owns the only sqlite connection. Every 10 seconds it rebuilds
  the whole catalog into an immutable `Catalog` and swaps it in; once a second
  it writes queued metrics events in one transaction.
- **HTTP thread** runs one io_context with a stackful coroutine per connection
  (boost::context — the same primitive Boost.Fiber uses). Handlers read the
  current snapshot and drop events in a queue. No request ever waits on disk.

The whole catalog lives in RAM. That is fine well past the point where the
product is proven, and the refresh is a handful of queries.

## Metrics

Outbound clicks go through `/r/{item_id}?p={position}`, which logs the click
and 302s to the partner with an opaque click token as `subid`. That token is
what an affiliate postback (`/cpa/postback`) matches against, closing the loop
from impression to revenue. Nothing identifying the visitor is ever sent to a
partner.

`ssize-admin stats --days 7` prints the funnel, top items by CTR, and
conversions.

## Deploy

`nix build` then CPack produces a `.deb`; `deploy/` has the systemd unit and
the nginx snippet. nginx serves `/static/` and `/media/` directly.
