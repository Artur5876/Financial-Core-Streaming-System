# FinCore Streaming System

FinCore is a C++20 market-data prototype. Its active application fetches quotes
from Alpha Vantage, caches them in Redis, derives a small synthetic order book,
and exposes the workflow and process metrics through an interactive CLI.

> [!IMPORTANT]
> PostgreSQL/TimescaleDB schema and client code are present in the repository,
> but the PostgreSQL client is not linked into the current `fincore_app` target.
> See [Project status](docs/project-status.md) for the exact implementation state.

## Active data flow

```text
CLI fetch
   |
   v
Redis quote cache -- miss --> Alpha Vantage client cache -- miss --> HTTP API
   |                                      |
   +--------------- quote <---------------+
                         |
                         +--> Redis quote + bounded history
                         +--> synthetic 5-level OrderBook
                                      |
                                      +--> Redis book snapshot
```

Redis quote entries expire after 60 seconds by default. Reads do not extend the
TTL. The Alpha Vantage client also has an in-process cache whose TTL is set from
`POLL_SECONDS` (60 seconds by default).

## Requirements

- Linux (process metrics read Linux `/proc` and `getrusage` data)
- CMake 3.20 or newer
- A C++20 compiler
- libcurl development files
- hiredis and redis-plus-plus
- Git and network access during the first CMake configure (GoogleTest is fetched)
- Redis 7, locally installed or run with Docker

PostgreSQL dependencies are only needed when developing the currently dormant
PostgreSQL client. They are not part of the active CMake build.

## Quick start

Start Redis:

```bash
docker compose -f docker/docker-compose.yml up -d redis
docker compose -f docker/docker-compose.yml exec redis redis-cli ping
```

Configure and build:

```bash
cmake -S . -B build
cmake --build build -j
```

Run with an Alpha Vantage API key:

```bash
AV_API_KEY=your_key ./build/fincore_app
```

The program defaults to Alpha Vantage's `demo` key, which may not serve the
hard-coded symbols. The active symbols are `AAPL`, `MSFT`, and `GOOGL` and are
currently defined in `src/core/main.cpp`; `config/symbols.txt` is not loaded.

At the prompt, try:

```text
redis status
fetch AAPL
book AAPL
stats
quit
```

## Runtime configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `AV_API_KEY` | `demo` | Alpha Vantage API key |
| `REDIS_HOST` | `127.0.0.1` | Redis host |
| `REDIS_PORT` | `6379` | Redis port |
| `REDIS_QUOTE_TTL_SECONDS` | `60` | Redis quote TTL |
| `POLL_SECONDS` | `60` | CLI polling default and API-client cache TTL |

Values for ports and durations are parsed as integers at startup. Invalid values
currently terminate the program.

## Test

```bash
ctest --test-dir build --output-on-failure
```

The Redis unit test is a serialization fake and does not connect to Redis. There
is currently no Redis or PostgreSQL integration test.

## Documentation

- [Architecture](docs/architecture.md)
- [Development and testing](docs/development.md)
- [CLI reference](docs/cli-reference.md)
- [Redis caching details](docs/redis-caching.md)
- [PostgreSQL schema design](docs/psql_architecture.md)
- [Project status and known gaps](docs/project-status.md)

## Repository layout

```text
include/             Public C++ headers
src/api/             Alpha Vantage HTTP client
src/cli/             Interactive CLI and process metrics
src/core/            Entry point and in-memory order book
src/storage/         Redis client and dormant PostgreSQL client
tests/unit/          GoogleTest test executables
scripts/migrations/  TimescaleDB/PostgreSQL schema
docker/              Compose configuration
docs/                Design and operator documentation
```

## Security note

`docker/docker-compose.yml` contains development-only PostgreSQL credentials.
Do not reuse them outside a local environment. The Alpha Vantage API key is read
from the environment; avoid committing it to the repository.
