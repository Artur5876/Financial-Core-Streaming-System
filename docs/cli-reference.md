# CLI Reference

Run the CLI with `./build/fincore_app`. Commands and symbols are
case-insensitive; symbols are normalized to uppercase.

The configured symbols are currently `AAPL`, `MSFT`, and `GOOGL`.

| Command | Description | Defaults and limits |
| --- | --- | --- |
| `help` | Print command help | No arguments |
| `symbols` | List configured symbols | No arguments |
| `fetch <SYMBOL\|all>` | Fetch, cache, build book, and print metrics | One target |
| `book <SYMBOL>` | Print the current in-memory book | Requires a prior fetch in this process |
| `lookup <SYMBOL> [COUNT]` | Compare vector and hash-map lookup time | Default 1,000,000; max 100,000,000 |
| `watch <SYMBOL\|all> [COUNT] [MS]` | Repeat fetches | Defaults: 10 iterations, 1,000 ms; max 100,000 and 3,600,000 ms |
| `poll [COUNT] [SECONDS]` | Fetch all symbols in cycles | Defaults: 1 cycle and `POLL_SECONDS`; max 100,000 and 86,400 s |
| `redis status` | Report client connection state | No network round-trip is guaranteed by this command |
| `stats` | Print aggregate session statistics | No arguments |
| `stats last` | Print the most recent measured operation | No additional arguments |
| `stats reset` | Clear session statistics | No additional arguments |
| `exit` / `quit` | Leave the CLI | No arguments |

## Examples

Fetch one quote and inspect its derived book:

```text
fincore> fetch AAPL
fincore> book AAPL
fincore> stats last
```

Run five all-symbol cycles ten seconds apart:

```text
fincore> poll 5 10
```

Fetch Microsoft ten times with a 250 ms interval:

```text
fincore> watch MSFT 10 250
```

## Fetch result semantics

A fetch is counted as successful only if both the quote state and order-book
snapshot are considered stored in Redis. A Redis quote cache hit counts as an
already-successful quote write; it does not refresh the TTL or append duplicate
history. The book is rebuilt and rewritten on every successful quote read.

`book` displays process-local state only. Restarting the application clears that
state even if Redis still contains an order-book snapshot.

## Metrics

Per-operation output distinguishes Redis cache, Alpha Vantage client cache, and
external API sources. The session summary reports successes/failures, cache hit
rate, Redis write results, latency distributions, throughput, CPU usage, RSS,
page faults, and context switches.

Metrics are observational development measurements, not a stable telemetry API.
Short operations may show zero-microsecond stages because of timer resolution.
