# Redis caching in FinCore

## What is implemented

The quote path uses the cache-aside pattern:

1. `fetch SYMBOL` asks Redis for `quote:SYMBOL`.
2. A cache hit skips Alpha Vantage.
3. A cache miss uses the existing Alpha Vantage client and stores the result in
   Redis with a TTL.
4. Both paths rebuild the in-memory order book and update its Redis snapshot.

The default quote TTL is 60 seconds. Override it when starting the app:

```bash
REDIS_QUOTE_TTL_SECONDS=120 ./build/fincore_app
```

The TTL is deliberately not refreshed on reads. This prevents an actively read
quote from remaining stale forever. A cache hit is also not appended to quote
history again.

**Be familiar with:** cache-aside, TTL, cache hit/miss, stale data.

**Search or ask another chat:** `Redis cache aside pattern`, `Redis TTL vs
sliding expiration`, `cache stampede basics`.

## Docker Compose

Start only Redis:

```bash
docker compose -f docker/docker-compose.yml up -d redis
docker compose -f docker/docker-compose.yml ps
docker compose -f docker/docker-compose.yml exec redis redis-cli ping
```

Stop the container while keeping data:

```bash
docker compose -f docker/docker-compose.yml down
```

Delete the named volumes too (this removes Redis and PostgreSQL data):

```bash
docker compose -f docker/docker-compose.yml down -v
```

The Redis port is bound to `127.0.0.1`, so it is available to this machine but
not exposed on every network interface. A health check verifies `PING`.
Append-only persistence is enabled because this project also stores bounded
quote history and tick streams; Redis is therefore doing more than disposable
caching.

**Be familiar with:** Compose services, named volumes, health checks, port
binding, Redis AOF persistence.

**Search or ask another chat:** `Docker Compose Redis healthcheck`, `Redis AOF
appendfsync everysec`, `127.0.0.1 Docker port binding`.

## Practical next ideas

### 1. Add a cache inspection CLI command

A useful next command is `redis get <SYMBOL>`, showing the cached quote and
remaining TTL. It helps debugging without requiring knowledge of `redis-cli`.
Keep it read-only.

**Be familiar with:** Redis `HGETALL`, `TTL`, CLI command parsing.

**Search or ask another chat:** `redis-cli HGETALL TTL`, `redis-plus-plus ttl`.

### 2. Bound tick streams

`store_tick` currently appends to `ticks:SYMBOL` without a maximum length.
For a long-running stream, use approximate `MAXLEN` trimming (for example,
retain the newest 100,000 ticks per symbol). Pick the number from an actual
retention requirement, not guesswork.

**Be familiar with:** Redis Streams, `XADD MAXLEN ~`, memory sizing.

**Search or ask another chat:** `Redis Streams capped MAXLEN approximate`,
`Redis stream memory usage`.

### 3. Make order-book replacement atomic

The current implementation deletes and recreates two hashes. A reader can
briefly observe an empty or half-updated book. When concurrent readers are
introduced, write a versioned snapshot or use a transaction/Lua script. This is
not necessary for the current single-process CLI.

**Be familiar with:** atomicity, Redis transactions, Lua scripts, snapshot
versioning.

**Search or ask another chat:** `Redis atomic replace hash`, `Redis MULTI EXEC
limitations`, `Redis Lua atomic update`.

### 4. Decide whether Redis is cache or durable event storage

Current quotes are cache entries, while history and streams are retained data.
This hybrid is reasonable for development, but production retention,
eviction, backups, and failure expectations should be documented separately.
Do not add replicas or Redis Cluster until load or availability requirements
justify them.

**Be familiar with:** cache versus system of record, eviction policies,
persistence, recovery objectives.

**Search or ask another chat:** `Redis cache vs primary database`, `Redis
maxmemory eviction policy`, `RPO RTO basics`.

## Current implementation notes

- `get_quote` was already implemented but was not connected to the CLI.
- Current quotes had no TTL, so they could become permanently stale.
- `update_order_book` swallowed Redis errors and returned `void`; the CLI
  consequently treated failed writes as successful. It now returns `bool`.
- Quote history is bounded to 1,000 entries, which is a sensible initial limit.
- Tick streams remain unbounded and should be capped before sustained ingestion.
- Redis connection failure still stops application startup. That is reasonable
  while Redis is required for a successful pipeline; graceful cache bypass can
  be added later if Redis becomes optional.
- The Redis unit test currently mirrors serialization in a fake implementation.
  A small integration test against the Compose service would give stronger
  confidence in TTLs and real Redis commands.
