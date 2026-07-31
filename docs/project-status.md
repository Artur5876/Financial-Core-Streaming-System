# Project Status and Known Gaps

This is a code-backed snapshot of the repository, not a product roadmap.

## Working through the active CMake graph

- C++20 interactive application and CLI
- Alpha Vantage `GLOBAL_QUOTE` requests through libcurl
- In-process and Redis quote caching
- Bounded Redis quote history (1,000 entries)
- Synthetic in-memory order books and Redis snapshots
- Linux process/resource metrics and session latency summaries
- Unit-test targets for the order book, API client, CLI, metrics, and Redis
  serialization logic

## Present but not integrated

- PostgreSQL/TimescaleDB migration and extensive repository/client source
- Tick streaming method in `RedisClient`
- `config/symbols.txt` and `data/sample_data.csv`
- Visual Paradigm diagrams under `docs/diagrams`

These components are not reached by `fincore_app` today.

## Confirmed repository inconsistencies

### Docker Compose

- The `dev` service expects `docker/Dockerfile`, which does not exist.
- PostgreSQL expects `docker/init.sql`, which does not exist.
- Compose creates database `mydb`, while the migration connects to `fincore`.
- Development PostgreSQL credentials are committed in plain text.

The Redis service can still be started independently.

### Build scripts

The manual shell build uses C++17, compiles every source file, and does not match
the CMake dependency or executable definitions. The run script names an
executable that CMake does not create. CMake is the only documented build path.

### PostgreSQL client

The client is excluded from CMake, so the normal build gives no compile-time
assurance for it. Inspection also shows duplicated namespace declarations and
duplicated/garbled SQL fragments. It should be treated as unfinished until it
has its own target and tests.

### Configuration

- Symbols are hard-coded in `main.cpp`; the symbols file is unused.
- Startup integer environment variables have no validation wrapper.
- Redis is mandatory at startup even though it is described primarily as a
  cache.

### Data and concurrency

- Order-book replacement in Redis is not atomic.
- Tick streams are unbounded and lack a retention policy.
- Latest ticks and order-book keys have no TTL.
- The order book is derived from a quote rather than real bid/ask market depth.
- Alpha Vantage response parsing is ad hoc rather than JSON-library based.
- `curl_global_init`/`curl_global_cleanup` are owned per client instance, which
  is unsuitable if multiple clients or threads are later introduced.

### Tests

- No Redis integration test validates actual key types, TTLs, or errors.
- No PostgreSQL migration/client integration tests exist.
- Redis unit tests reproduce serialization in the test rather than call
  production serialization code.
- Docker and shell scripts are not exercised by automated tests.

## Suggested stabilization order

1. Make the documented CMake build reproducible and align or remove stale shell
   scripts.
2. Repair Compose paths, database naming, and secret handling.
3. Add a real Redis integration test.
4. Decide whether PostgreSQL is in scope; if yes, add a dedicated library target
   and make it compile before connecting it to the CLI.
5. Load symbols from configuration and validate startup settings.
6. Define retention and atomicity requirements before sustained ingestion.
