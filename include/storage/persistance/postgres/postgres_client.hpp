// Patterns used
//   Builder     - ConnectionConfig::builder().host("…").pool_size(somenumber).build()
//   Repository  - one focused class per domain; no god-object
//   Template Method - Repository::fetch_all/fetch_one/fetch_scalar/execute
//                     handle the full acquire→exec→map→release lifecycle once
//   Facade      - PostgresClient owns the pool and wires the repos together
//
#pragma once
#include "core/types.hpp"
#include "core/types.hpp"
#include <chrono>
#include <functional>
#include <libpq-fe.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>


namespace fincore::db {

//------------------------------
//Result type
//------------------------------
namespace fincore::db {

struct DbError {
    std::string message;
    std::string sqlstate; //5 digits normal
    std::string query; //failing query
};

template <typename T> using DbResult = std::variant<T, DbError>;

template <typename T> [[nodiscard]] bool            is_ok    (const DbResult<T>& r) noexcept { return std::holds_alternative<T>(r); }
template <typename T> [[nodiscard]] const T&        unwrap   (const DbResult<T>& r)           { return std::get<T>(r); }
template <typename T> [[nodiscard]] T&              unwrap   (DbResult<T>& r)                  { return std::get<T>(r); }
template <typename T> [[nodiscard]] const DbError&  error_of (const DbResult<T>& r)           { return std::get<DbError>(r); }

//------------------------------
//domain structs
//------------------------------
struct Instrument {
    Symbol      symbol;
    std::string name, asset_class, exchange;// asset_class: EQUITY|FOREX|CRYPTO|FUTURES|ETF
    int         tick_size_decimals{4};
    bool        is_active{true};
};

struct Candle {
    Symbol    symbol;
    TimePoint bucket;                          // start of the interval
    double    open{}, high{}, low{}, close{};
    uint64_t  volume{};
    uint32_t  trade_count{};
};

struct IndicatorPoint {
    TimePoint   timestamp;
    double      value{};
    std::string parameters;                    // JSON, JSONB{"period":14}
};

struct SpreadStats {
    Symbol   symbol;
    double   avg_spread{}, min_spread{}, max_spread{};
    double   avg_imbalance{}, avg_mid{};
    uint64_t sample_count{};
};

struct HourlyBookRow {
    TimePoint bucket;
    double    avg_spread{}, min_spread{}, max_spread{};
    double    avg_imbalance{}, avg_mid{};
};

struct DataFreshness {
    Symbol                   symbol;
    std::optional<TimePoint> last_quote, last_tick, last_snapshot, last_indicator;
};

struct PoolStats {
    std::size_t total{}, idle{}, busy{}, failed_acquisitions{};
    uint64_t    queries_ok{}, queries_failed{};
    std::chrono::microseconds avg_latency{};
};

struct CompressionStats {
    std::string hypertable;
    std::size_t uncompressed_bytes{}, compressed_bytes{};
    double      ratio{};
    uint32_t    total_chunks{}, compressed_chunks{};
};

struct ChunkInfo {
    std::string hypertable, chunk_name;
    TimePoint   range_start, range_end;
    std::size_t size_bytes{};
    bool        is_compressed{};
};

struct PriceGap   { TimePoint bucket; double prev_close{}, open{}, gap_pct{}; };
struct QueryStat  { std::string query; uint64_t calls{}, rows{};
    double total_ms{}, mean_ms{}; };
struct DailyStats { std::string symbol, date; std::size_t tick_count{};
    double open{}, high{}, low{}, close{}, vwap{}; uint64_t volume{}; };

//------------------------------
//Transaction(RAII) - rolls back automatically unless committed
//------------------------------
class Transaction {
public:
    virtual ~Transaction()                               = default;
    virtual DbResult<bool> commit()                     = 0;
    virtual DbResult<bool> rollback()                    = 0;
    [[nodiscard]] virtual bool is_active() const noexcept = 0;
};

//------------------------------
//ConnectionConfig  — Builder pattern for readable, safe construction
//   auto cfg = ConnectionConfig::builder().host("db.prod").pool_size(16)
//      .ssl_required().build();
//------------------------------
struct ConnectionConfig {
    std::string host{"localhost"};
    int         port{5432};
    std::string dbname{"fincore"};
    std::string user{"fincore_app"};
    std::string password;
    int         pool_size{8};
    int         connect_timeout_s{5};
    int         query_timeout_ms{5000};
    std::string ssl_mode{"prefer"};    //Modes: disable|allow|prefer|require
    std::string app_name{"fincore_cpp"};

    class Builder {
    public:
        Builder& host        (std::string v) { c_.host       = std::move(v); return *this; }
        Builder& port        (int v)          { c_.port       = v;            return *this; }
        Builder& database    (std::string v) { c_.dbname     = std::move(v); return *this; }
        Builder& user        (std::string v) { c_.user       = std::move(v); return *this; }
        Builder& password    (std::string v) { c_.password   = std::move(v); return *this; }
        Builder& pool_size   (int v)          { c_.pool_size  = v;            return *this; }
        Builder& timeout_ms  (int v)          { c_.query_timeout_ms = v;      return *this; }
        Builder& app_name    (std::string v) { c_.app_name   = std::move(v); return *this; }
        Builder& ssl_required()               { c_.ssl_mode   = "require";    return *this; }
        Builder& ssl_disabled()               { c_.ssl_mode   = "disable";    return *this; }
        ConnectionConfig build() { return std::move(c_); }
    private:
        ConnectionConfig c_;
    };

    static Builder builder() { return {}; }
};

//------------------------------
// Repository base
//
//   Owns a shared ConnectionPool reference.  Provides four template methods
//   that handle the full acquire → exec → map → release lifecycle in one place,
//   so every concrete repository method is just a 2–4 line call.
//
//   // Typical usage in a concrete repo:
//   DbResult<std::vector<Tick>> TickRepo::range(…) {
//       return fetch_all<Tick>(kSelectTicks, {sym, from, to, limit}, from_row);
//   }
//------------------------------
class ConnectionPool;  // defined in .cpp
using Params    = std::vector<std::string>;
using RowMapper = std::function<void*(PGresult*, int)>;  // typed via templates below

class Repository {
public:
    explicit Repository(std::shared_ptr<ConnectionPool> pool);
    virtual ~Repository() = default;

protected:
    //RawResult: RAII wrapper for PGresult*.
    // run() acquires a connection, calls PQexecParams, releases the connection,
    // and returns the result.  Templates below call run() — nothing else does.
    struct RawResult {
        PGresult*   res{nullptr};
        DbError     err;
        bool        ok{false};
        ~RawResult() { if (res) PQclear(res); }
        RawResult(RawResult&& o) noexcept
            : res(o.res), err(std::move(o.err)), ok(o.ok) { o.res = nullptr; }
        RawResult& operator=(RawResult&&) = delete;
        RawResult(const RawResult&)       = delete;
    };

    //namespace (Params)
    RawResult run(const std::string& sql, const Params& params) const;

    //===Template methods (defined inline here; call only run())===

    // Fetch N rows and map each with `mapper(PGresult*, row_index) → T`.
    template <typename T>
    DbResult<std::vector<T>> fetch_all(
        const std::string&          sql,
        const Params&               params,
        std::function<T(PGresult*, int)> mapper) const
    {
        auto q = run(sql, params);
        if (!q.ok) return q.err;
        std::vector<T> out;
        out.reserve(PQntuples(q.res));
        for (int i = 0; i < PQntuples(q.res); ++i)
            out.push_back(mapper(q.res, i));
        return out;
    }

    // Fetch 0 or 1 rows.  Returns nullopt on empty result set.
    template <typename T>
    DbResult<std::optional<T>> fetch_one(
        const std::string&               sql,
        const Params&                    params,
        std::function<T(PGresult*, int)> mapper) const
    {
        auto r = fetch_all<T>(sql, params, std::move(mapper));
        if (!is_ok(r)) return error_of(r);
        auto& v = unwrap(r);
        if (v.empty()) return std::optional<T>{};
        return std::optional<T>(std::move(v.front()));
    }

    // Fetch a single scalar value from column 0.  Returns `fallback` on NULL.
    template <typename T>
    DbResult<T> fetch_scalar(
        const std::string& sql,
        const Params&      params,
        T                  fallback = {}) const
    {
        auto q = run(sql, params);
        if (!q.ok) return q.err;
        if (PQntuples(q.res) == 0 || PQgetisnull(q.res, 0, 0)) return fallback;
        if constexpr (std::is_same_v<T, double>)
            return std::stod(PQgetvalue(q.res, 0, 0));
        else if constexpr (std::is_integral_v<T>)
            return static_cast<T>(std::stoll(PQgetvalue(q.res, 0, 0)));
        else
            return T{ PQgetvalue(q.res, 0, 0) };
    }

    // Execute a non-SELECT statement (INSERT, UPDATE, CALL, …).
    DbResult<bool> execute(const std::string& sql, const Params& params) const
    {
        auto q = run(sql, params);
        if (!q.ok) return q.err;
        return true;
    }

    // Bulk insert via COPY FROM STDIN (tab-separated rows).
    DbResult<std::size_t> copy_bulk(const std::string& table,
                                    const std::string& columns,
                                    std::string_view   tsv) const;

    // === Column helpers (use inside mapper lambdas) ====================
    static double      col_d  (PGresult* r, int row, int c);
    static uint64_t    col_u64(PGresult* r, int row, int c);
    static int         col_i  (PGresult* r, int row, int c);
    static bool        col_b  (PGresult* r, int row, int c);
    static std::string col_s  (PGresult* r, int row, int c);
    static TimePoint   col_ts (PGresult* r, int row, int c);
    static std::optional<TimePoint> col_ts_opt(PGresult* r, int row, int c);
    static std::optional<double>    col_d_opt (PGresult* r, int row, int c);

    // === Timestamp / interval conversion helpers===========
    static std::string to_pg_ts      (TimePoint tp);
    static TimePoint   from_pg_ts    (const char* s);
    static std::string to_pg_interval(std::chrono::seconds s);

    std::shared_ptr<ConnectionPool> pool_;
};

//===========================================
//Concrete repositories
//========================================================

// === Instruments ==============================================
class InstrumentRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<bool>                      upsert     (const Instrument& inst);
    DbResult<std::size_t>               upsert_many(std::span<const Instrument> batch);
    DbResult<std::vector<Instrument>>   all        (bool active_only = true);
    DbResult<std::optional<Instrument>> find       (const Symbol& symbol);
    DbResult<bool>                      set_active (const Symbol& symbol, bool active);

private:
    static Instrument from_row(PGresult* r, int i);
};

// ===Quotes =======
class QuoteRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<bool>               store      (const Symbol& sym, const Quote& q);
    DbResult<std::size_t>        store_many (std::span<const Quote> quotes);
    DbResult<std::vector<Quote>> range      (const Symbol& sym, TimePoint from, TimePoint to,
                                             std::size_t limit = 1000);
    DbResult<std::optional<Quote>>                      latest       (const Symbol& sym);
    DbResult<double>                                    latest_price (const Symbol& sym);
    DbResult<std::unordered_map<Symbol, double>>        latest_prices(std::span<const Symbol> syms);

private:
    static Quote       from_row(PGresult* r, int i);
    static std::string to_tsv  (const Symbol& sym, const Quote& q);
};

// === Ticks ==================================
class TickRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<bool>               store    (const Tick& tick);
    DbResult<std::size_t>        store_many(std::span<const Tick> ticks);
    DbResult<std::vector<Tick>>  range    (const Symbol& sym, TimePoint from, TimePoint to,
                                           std::size_t limit = 10000);
    DbResult<uint64_t>           count    (const Symbol& sym, TimePoint from, TimePoint to);
    DbResult<double>             vwap     (const Symbol& sym, TimePoint from, TimePoint to);

    //candles built on-the-fly (time_bucket) or from continuous aggregate.
    DbResult<std::vector<Candle>> candles   (const Symbol& sym, std::chrono::seconds interval,
                                             TimePoint from, TimePoint to, std::size_t limit = 500);
    DbResult<std::vector<Candle>> daily_ohlcv(const Symbol& sym,
                                              const std::string& from_date,
                                              const std::string& to_date,
                                              std::size_t limit = 365);

    // Pull-based stream: invoke handler per new tick since `since`.
    // Returns the timestamp of the last delivered row (use as next cursor).
    using OnTick = std::function<void(const Tick&)>;
    DbResult<TimePoint> stream(const Symbol& sym, TimePoint since,
                               OnTick handler, std::size_t batch = 500);

private:
    static Tick        from_row   (PGresult* r, int i);
    static Candle      candle_row (PGresult* r, int i);
    static std::string to_tsv     (const Tick& t);
};

// === Order book ==================================
class OrderBookRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<bool> store(const Symbol& sym,
                         double best_bid, double best_ask,
                         double imbalance,
                         uint64_t bid_vol, uint64_t ask_vol);

    DbResult<std::optional<OrderBookSnapshot>> latest      (const Symbol& sym);
    DbResult<SpreadStats>                      spread_stats(const Symbol& sym,
                                                             TimePoint from, TimePoint to);
    DbResult<std::vector<HourlyBookRow>>       hourly      (const Symbol& sym,
                                                             const std::string& from_date,
                                                             const std::string& to_date);
private:
    static OrderBookSnapshot snapshot_row(PGresult* r, int i);
    static HourlyBookRow     hourly_row  (PGresult* r, int i);
};

// === Technical indicators ====================================================
class IndicatorRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<bool>        store     (const Symbol& sym, const std::string& name,
                                     double value, const std::string& params_json = "{}");
    DbResult<std::size_t> store_many(const Symbol& sym, const std::string& name,
                                     std::span<const IndicatorPoint> points);

    DbResult<std::optional<double>>              last_value(const Symbol& sym, const std::string& name);
    DbResult<std::vector<IndicatorPoint>>        series    (const Symbol& sym, const std::string& name,
                                                             TimePoint from, TimePoint to,
                                                             std::size_t limit = 1000);
    // All latest values for one symbol:  {"RSI":62.3, "EMA":154.2}
    DbResult<std::unordered_map<std::string, double>> latest_all(const Symbol& sym);
    // Latest value of one indicator across all active symbols (screener).
    DbResult<std::unordered_map<Symbol, double>>      snapshot  (const std::string& name);

private:
    static IndicatorPoint from_row(PGresult* r, int i);
    static std::string    to_tsv  (const Symbol& sym, const std::string& name,
                                   const IndicatorPoint& pt);
};

// ===Analytics + observability ===================================
class AnalyticsRepo : public Repository {
public:
    using Repository::Repository;

    DbResult<std::vector<DailyStats>>                  daily_stats   (const Symbol& sym,
                                                                       const std::string& from_date,
                                                                       const std::string& to_date);
    DbResult<double>                                   rolling_return(const Symbol& sym, int days);
    DbResult<double>                                   realised_vol  (const Symbol& sym, int days);
    DbResult<std::vector<std::pair<Symbol, uint64_t>>> most_active   (TimePoint from, TimePoint to,
                                                                       std::size_t top_n = 10);
    DbResult<std::vector<PriceGap>>                    price_gaps    (const Symbol& sym,
                                                                       double threshold_pct,
                                                                       TimePoint from, TimePoint to);
    // Observability
    DbResult<DataFreshness>                freshness        (const Symbol& sym);
    DbResult<std::vector<CompressionStats>> compression     ();
    DbResult<std::vector<ChunkInfo>>        chunks          (const std::string& hypertable);
    DbResult<uint32_t>                      compress_old    (const std::string& hypertable,
                                                             std::chrono::system_clock::duration older_than);
    DbResult<bool>                          refresh         (const std::string& view,
                                                             TimePoint from, TimePoint to);
    DbResult<bool>                          refresh_all     ();
    DbResult<std::vector<QueryStat>>        slow_queries    (double min_mean_ms = 100.0,
                                                             std::size_t limit  = 20);
private:
    static DailyStats  daily_row  (PGresult* r, int i);
    static PriceGap    gap_row    (PGresult* r, int i);
    static ChunkInfo   chunk_row  (PGresult* r, int i);
    static QueryStat   stat_row   (PGresult* r, int i);
};

//------------------------------------------
// § 7  PostgresClient  — Facade
//
//   Owns the ConnectionPool and creates all repositories over it.
//   Call sites look like:
//
//     PostgresClient db(cfg);
//     db.ticks().store_many(batch);
//     db.analytics().rolling_return("AAPL", 30);
//
//     db.transaction([&]{
//         db.ticks().store(t);
//         db.indicators().store("AAPL", "RSI", 63.1);
//     });
//------------------------------------------------
class PostgresClient {
public:
    explicit PostgresClient(ConnectionConfig cfg = {});
    ~PostgresClient();

    PostgresClient(const PostgresClient&)            = delete;
    PostgresClient& operator=(const PostgresClient&) = delete;
    PostgresClient(PostgresClient&&)                 = default;
    PostgresClient& operator=(PostgresClient&&)      = default;

    // === Repository accessors =========================
    InstrumentRepo& instruments() { return *instruments_; }
    QuoteRepo&      quotes()      { return *quotes_;      }
    TickRepo&       ticks()       { return *ticks_;       }
    OrderBookRepo&  order_book()  { return *order_book_;  }
    IndicatorRepo&  indicators()  { return *indicators_;  }
    AnalyticsRepo&  analytics()   { return *analytics_;   }

    // === Pool-level operations ================================
    [[nodiscard]] bool      ping()       const;
    [[nodiscard]] PoolStats pool_stats() const;
    void                    reconnect();

    // === Transactions =====================================
    [[nodiscard]] std::unique_ptr<Transaction> begin_transaction();

    // Runs fn() in a transaction; auto-commits on success, rolls back on throw.
    DbResult<bool> transaction(std::function<void()> fn);

    // Warm up prepared statements for the hot insert paths.
    void prepare_statements();

private:
    std::shared_ptr<ConnectionPool> pool_;
    std::unique_ptr<InstrumentRepo> instruments_;
    std::unique_ptr<QuoteRepo>      quotes_;
    std::unique_ptr<TickRepo>       ticks_;
    std::unique_ptr<OrderBookRepo>  order_book_;
    std::unique_ptr<IndicatorRepo>  indicators_;
    std::unique_ptr<AnalyticsRepo>  analytics_;
};

}  // namespace fincore::db


}

//asset_class[EQUITY, EXCHANGE, FUTURES OR ETFS]
struct Instrument {
    Symbol      symbol;
    std::string name, asset_class, exchange;
    int         tick_size_decimals{4}; //default: 4(int);
    bool is_active{true};
};


struct Candle {
    Symbol symbol;
    TimePoint bucket; //start of the interval

};



