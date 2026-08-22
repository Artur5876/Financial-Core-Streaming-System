# Configuration and Operation

## Build integration

CMake requires PostgreSQL and builds `postgres_persistence` from the connection and repository sources. The library links `fincore_domain` and `PostgreSQL::PostgreSQL`.

`fincore_app` links `postgres_persistence`; its `postgres` command mode exposes
repository writes and latest-quote reads. Running it without arguments retains
the interactive Redis/API workflow and also persists fetched quotes and derived
order-book snapshots to PostgreSQL.

## Runtime configuration

`ConnectionConfig::from_environment()` reads:

| Variable | Default | Meaning |
|---|---|---|
| `FINCORE_DB_HOST` | `127.0.0.1` | Database host |
| `FINCORE_DB_PORT` | `5432` | Database port |
| `FINCORE_DB_NAME` | `fincore` | Database name |
| `FINCORE_DB_USER` | `fincore_app` | Login role |
| `FINCORE_DB_PASSWORD` | empty | Login password |
| `FINCORE_DB_APPLICATION_NAME` | `fincore-cpp` | PostgreSQL session application name |
| `FINCORE_DB_CONNECT_TIMEOUT` | `5` | Positive timeout in seconds |

The timeout parser rejects empty-invalid, non-numeric, zero, and negative explicit values. The connection session is switched to UTC immediately after connecting.

## Intended startup order

1. Start a PostgreSQL;
2. Create the `fincore` database or connect with sufficient privileges.
3. Apply `migration/schema.sql`.
4. Provide credentials through `FINCORE_DB_*` environment variables.
5. Construct `ConnectionConfig`, then `PostgresMarketDataRepository`.
6. Upsert referenced instruments and data sources before inserting time-series rows.

The repository prepares its SQL statements during construction, so a missing table, column, or schema fails early.

## Error and transaction behavior

- Connection and query failures throw `DatabaseError`.
- PostgreSQL SQLSTATE is retained when the server supplies one.
- An invalid environment timeout throws `std::invalid_argument`.
- Batch domain validation happens before `BEGIN`, avoiding a partially started write for invalid input.
- A SQL failure during a batch unwinds the stack and triggers best-effort rollback.
- Empty batches return immediately without opening a transaction.

## Deployment notes

The current Compose service uses `postgres:15-alpine`, which does not provide TimescaleDB by default, and mounts `docker/init.sql`, which is absent. Its database name and credentials also differ from the C++ defaults. Treat Compose as unfinished until these points and the items in [known gaps](known-gaps.md) are aligned.

For production, inject secrets externally, restrict network exposure, use a TimescaleDB-compatible PostgreSQL image/version, and run versioned migrations through a deployment process rather than embedding default passwords in SQL.
