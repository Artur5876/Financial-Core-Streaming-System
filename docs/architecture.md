# Architecture

## Scope

This document separates the application that is built today from components
that exist only as design or unlinked source code.

## Runtime components

### `fincore_app`

`src/core/main.cpp` is the composition root. It reads environment variables,
constructs `AlphaVantageClient` and `RedisClient`, and passes both to
`cli::FinCoreCli`. Redis is currently mandatory: failure to construct its client
ends startup with status 1.

### Interactive CLI

`FinCoreCli` owns one in-memory `OrderBook` per configured symbol and orchestrates
quote retrieval. Its service functions wrap the concrete API and Redis clients,
which lets unit tests supply fakes.

For each `fetch`:

1. Read `quote:<SYMBOL>` from Redis.
2. On a miss, call `AlphaVantageClient::get_quote`.
3. On an external/API-client result, write the quote to Redis.
4. Generate five bid and five ask levels around the quote price at $0.01 steps.
5. Replace the in-memory book and write its bid/ask hashes to Redis.
6. Record latency, cache, CPU, memory, fault, and context-switch measurements.

The generated book is illustrative, not exchange depth. Each level uses
`quote.volume / 10 / level` as its volume.

### Alpha Vantage client

The client uses libcurl and the `GLOBAL_QUOTE` endpoint. It has an in-process,
per-symbol cache and returns `std::nullopt` for empty symbols, transport errors,
non-200 responses, known API error bodies, or parse failures.

The JSON parser is a small string-based extractor rather than a general JSON
parser. This keeps dependencies small but makes parsing sensitive to response
format changes.

### Redis client

The Redis implementation uses redis-plus-plus and hiredis. It provides quote
caching, bounded quote history, tick stream writes, and order-book snapshots.

| Key | Type | Retention | Writer |
| --- | --- | --- | --- |
| `quote:<SYMBOL>` | hash | Configurable TTL | `store_quote` |
| `quote_history:<SYMBOL>` | list | Newest 1,000 | `store_quote` |
| `ticks:<SYMBOL>` | stream | Unbounded | `store_tick` |
| `latest_tick:<SYMBOL>` | string | No expiry | `store_tick` |
| `order_book:<SYMBOL>:bids` | hash | No expiry | `update_order_book` |
| `order_book:<SYMBOL>:asks` | hash | No expiry | `update_order_book` |
| `order_book:<SYMBOL>:timestamp` | string | No expiry | `update_order_book` |

Book replacement deletes and reconstructs two hashes, so it is not atomic for
concurrent readers.

### Order book

`OrderBook` stores bids in descending-price order and asks in ascending-price
order. It supports level replacement/removal, best prices, spread, mid-price,
total volumes, imbalance, snapshots, and clearing. Non-positive prices are
rejected; a zero volume removes or omits a level.

### Process metrics

The CLI measures each operation with a steady clock and Linux process resource
data. Session statistics include cache hit counts, write outcomes, latency
percentiles, CPU time, resident memory, page faults, and context switches.

## Build graph

```text
fincore_app
  `-- fincore_cli
      |-- order_book
      |-- alpha_vantage_client -- libcurl
      |-- redis_client --------- redis-plus-plus/hiredis
      `-- process_metrics
```

GoogleTest supplies five separate unit-test executables through CMake
`FetchContent`.

## PostgreSQL/TimescaleDB track

`scripts/migrations/init_db.sql`, `include/storage/postgres_client.hpp`, and
`src/storage/postgres_client.cpp` describe a larger persistence subsystem with
repositories, pooling, transactions, analytics, continuous aggregates,
compression, and retention policies.

This track is not currently part of the runtime graph:

- CMake does not compile or link `postgres_client.cpp`.
- `main.cpp` does not construct `PostgresClient`.
- The CLI has no PostgreSQL commands or write path.
- Compose initialization points at a file that is not present.

Treat the PostgreSQL documentation as intended architecture until this source is
made buildable, connected, and integration-tested.
