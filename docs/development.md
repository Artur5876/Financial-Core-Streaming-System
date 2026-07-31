# Development and Testing

## Supported build path

Use CMake as the authoritative build system:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The first configure fetches GoogleTest v1.14.0 from GitHub. An offline first
configure will therefore fail unless that dependency is already available.

The build enables `-Wall -Wextra -Wpedantic` and requires C++20. CMake searches
for packaged redis-plus-plus/hiredis targets first, then common `/usr` and
`/usr/local` locations.

## Dependencies

On a Debian-family system, the active application needs equivalents of:

- `build-essential`, `cmake`, and `git`
- libcurl development headers
- hiredis development headers/library
- redis-plus-plus headers/library
- Redis tools or Docker for a runtime Redis instance

The existing `scripts/install_deps.sh` is not complete for the current CMake
build: it omits CMake, libcurl, redis-plus-plus, and Git. Review it before use
because it runs package installation through `sudo`.

## Running Redis

```bash
docker compose -f docker/docker-compose.yml up -d redis
docker compose -f docker/docker-compose.yml ps
docker compose -f docker/docker-compose.yml exec redis redis-cli ping
```

Stop the service while retaining its named volume:

```bash
docker compose -f docker/docker-compose.yml down
```

Adding `-v` deletes the Redis and PostgreSQL named volumes and their data.

## Unit tests

| Target | Coverage |
| --- | --- |
| `order_book_test` | Book mutations, validation, prices, volume, snapshots |
| `process_metrics_test` | Deltas, formatting, session summaries/percentiles |
| `fincore_cli_test` | Command parsing and orchestration with service fakes |
| `alpha_vantage_client_test` | Parsing and client-cache behavior with fake fetches |
| `test_redis_client` | Stand-alone serialization logic fake |

Run one suite with CTest's regex filtering, for example:

```bash
ctest --test-dir build -R order_book --output-on-failure
```

The Redis tests duplicate serialization logic instead of exercising
`RedisClient`; they cannot validate real commands, expiry, or connection errors.

## Scripts that need reconciliation

The scripts predate the current CMake target layout:

- `scripts/build.sh` compiles all `.cpp` files as C++17 and links different
  dependencies. This conflicts with the C++20 CMake project and includes the
  currently unintegrated PostgreSQL source.
- `scripts/build_and_run.sh` expects
  `build/Financial-Core-Streaming-Project`, while CMake creates
  `build/fincore_app`.
- `scripts/run_docker.sh` uses the legacy `docker-compose` command and attempts
  to start all Compose services.
- The Compose `dev` service references `docker/Dockerfile`, which is absent.
- The Compose PostgreSQL volume references `docker/init.sql`, which is absent;
  the repository schema is at `scripts/migrations/init_db.sql`.

Until those are fixed, use the explicit CMake and Redis-only Compose commands in
this document.

## Change checklist

Before submitting a change:

1. Configure and build through CMake.
2. Run all unit tests.
3. If Redis behavior changed, manually exercise it against the Compose service.
4. Update the key schema and CLI docs when commands or stored fields change.
5. Keep secrets and local build output out of version control.
