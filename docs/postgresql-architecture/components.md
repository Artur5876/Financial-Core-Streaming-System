# Component Architecture

## Persistence port

`MarketDataRepository` is the application-facing abstraction. It exposes:

- metadata upserts for instruments and data sources;
- batch inserts for quotes, ticks, order-book snapshots, and technical indicators;
- lookup of the latest quote by symbol and source.

Batch methods return `InsertSummary`. `attempted` is the input size, `inserted` is the number of affected rows, and their difference represents records ignored as duplicates.

## PostgreSQL adapter

`PostgresMarketDataRepository` implements the port and owns a `std::unique_ptr<PgConnection>`. Construction requires an open connection and prepares seven named statements:

| Statement | Purpose |
|---|---|
| `fincore_upsert_instrument` | Insert or update instrument metadata |
| `fincore_upsert_data_source` | Insert or update source metadata |
| `fincore_insert_quote` | Insert a quote and ignore a duplicate natural key |
| `fincore_insert_tick` | Insert a trade tick and ignore a duplicate trade ID |
| `fincore_insert_snapshot` | Insert an order-book snapshot |
| `fincore_insert_indicator` | Insert a technical indicator |
| `fincore_latest_quote` | Read the newest quote for a symbol and source |

All values are supplied as parameters rather than interpolated SQL. Domain objects are validated before a transaction begins.

## Connection layer

`PgConnection` is an RAII wrapper over `PGconn` and provides direct, parameterized, and prepared execution. It:

- connects with `PQconnectdbParams`;
- sets the session timezone to UTC;
- accepts command and tuple results;
- converts PostgreSQL failures into `DatabaseError` with SQLSTATE when available;
- closes the native handle on destruction or move assignment.

`PgResult` owns a `PGresult`, offers bounds-checked text access, preserves SQL `NULL`, and parses affected-row counts.

`PgTransaction` starts with `BEGIN`, commits explicitly, and attempts `ROLLBACK` in its destructor if still active. This gives batch inserts all-or-nothing behavior when validation or SQL execution fails.

## Write flow

sequenceDiagram
    participant S as Service
    participant R as Repository
    participant V as Domain validation
    participant C as PgConnection
    participant P as PostgreSQL

    S->>R: insert_quotes(batch)
    loop every object
        R->>V: validate(object)
    end
    R->>C: BEGIN
    C->>P: BEGIN
    loop every object
        R->>C: execute_prepared(parameters)
        C->>P: prepared INSERT ... ON CONFLICT
        P-->>C: affected rows
    end
    R->>C: COMMIT
    C->>P: COMMIT
    R-->>S: InsertSummary
```

The batch implementation sends one prepared execution per record inside one transaction. It is safe and simple, but it is not a PostgreSQL bulk-loading path such as `COPY` or a multi-row insert.

## Concurrency boundary

One repository owns one connection and is not thread-safe. A caller should create one repository per concurrent database writer, or place serialization/pooling above the repository. Connection pooling is not implemented in this layer.
