# Known Integration Gaps

These findings compare the current C++ PostgreSQL adapter, migration, CMake wiring, and Docker Compose configuration.

## Schema status

`migration/schema.sql` now matches the repository's `fincore` namespace,
source and trade identity columns, foreign keys, and conflict keys. Numeric
bounds between the fixed-point domain model and PostgreSQL columns should still
be verified explicitly before production use.

## Runtime and deployment differences

- C++ defaults to database `fincore` and user `fincore_app`; Compose creates database `mydb` and user `StaticGhost`.
- Compose mounts `./docker/init.sql` relative to the Compose file, effectively expecting `docker/docker/init.sql`; no such file exists. The actual migration is `migration/schema.sql`.
- Compose contains development credentials in plain text.

## Application integration and verification

- `postgres_persistence` is built but not linked into `fincore_app`.
- The `fincore_app postgres` command mode constructs
  `PostgresMarketDataRepository`; the interactive Redis/API workflow still does
  not write to PostgreSQL.
- The command layer has unit tests, but no disposable-database PostgreSQL
  integration test is present.
- No connection pool or retry policy is implemented.
- Batches use one round trip per row; performance may require multi-row inserts, pipelining, or `COPY` at higher ingest rates.

## Recommended resolution order

1. Align Docker database, user, migration mount, and environment configuration.
2. Add a disposable-database integration test covering migration, metadata
   upserts, duplicate inserts, rollback, and latest-quote reads.
3. Wire the repository into the streaming application if PostgreSQL should be
   part of that ingestion path.
