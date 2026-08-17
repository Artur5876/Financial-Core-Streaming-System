# Known Integration Gaps

These findings compare the current C++ PostgreSQL adapter, migration, CMake wiring, and Docker Compose configuration.

## Blocking schema differences

1. The repository qualifies every table with the `fincore` schema, but the migration creates tables in the current/default schema and never creates `fincore`.
2. Tick inserts require `trade_id`; the migration's `ticks` table has no `trade_id` column.
3. Snapshot inserts require `source`; the migration's `order_book_snapshots` table has no `source` column or foreign key.
4. Indicator inserts require `source`; the migration's `technical_indicators` table has no `source` column or foreign key.
5. Repository `ON CONFLICT` clauses require unique keys for quotes, ticks, snapshots, and indicators. The migration defines none of those unique constraints/indexes.
6. The repository always supplies quote OHLC and volume values, while the domain-to-database numeric ranges and types should be verified explicitly before production use.

Until items 1–5 are fixed, repository construction or write execution will fail against this migration.

## Runtime and deployment differences

- C++ defaults to database `fincore` and user `fincore_app`; Compose creates database `mydb` and user `StaticGhost`.
- Compose mounts `./docker/init.sql` relative to the Compose file, effectively expecting `docker/docker/init.sql`; no such file exists. The actual migration is `scripts/migrations/init_db.sql`.
- Compose uses `postgres:15-alpine`, while the migration immediately requests TimescaleDB. A standard PostgreSQL image does not include that extension.
- The migration begins with `\c fincore`, so it assumes the database already exists and is being applied through a `psql`-compatible runner.
- The migration embeds placeholder application passwords, and Compose contains development credentials in plain text.

## Application integration and verification

- `postgres_persistence` is built but not linked into `fincore_app`.
- No application composition code constructs `PostgresMarketDataRepository`.
- No PostgreSQL unit or integration test is present.
- No connection pool or retry policy is implemented.
- Batches use one round trip per row; performance may require multi-row inserts, pipelining, or `COPY` at higher ingest rates.

## Recommended resolution order

1. Choose one schema namespace (`fincore` or `public`) and align migration and prepared SQL.
2. Add the missing source/trade identity columns and foreign keys.
3. Add TimescaleDB-compatible unique indexes matching each `ON CONFLICT` target.
4. Align Docker image, database, user, migration mount, and environment configuration.
5. Add a disposable-database integration test covering migration, metadata upserts, duplicate inserts, rollback, and latest-quote reads.
6. Wire the repository into the application only after the persistence contract passes integration tests.
