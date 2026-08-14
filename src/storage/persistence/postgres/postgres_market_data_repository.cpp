#include "storage/persistance/postgres/postgres_market_data_repository.hpp"

#include "domain/validation.hpp"

#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fincore::persistence::postgres {
namespace {

using domain::Decimal;
using domain::Timestamp;

[[nodiscard]] std::string timestamp_to_utc_text(Timestamp timestamp) {
    using namespace std::chrono;

    const auto micros = floor<microseconds>(timestamp);
    const auto seconds_part = floor<seconds>(micros);
    const auto fractional = duration_cast<microseconds>(micros - seconds_part).count();
    const std::time_t time = system_clock::to_time_t(seconds_part);

    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &time) != 0) {
        throw std::runtime_error("failed to convert timestamp to UTC");
    }
#else
    if (gmtime_r(&time, &utc) == nullptr) {
        throw std::runtime_error("failed to convert timestamp to UTC");
    }
#endif

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(6) << std::setfill('0') << fractional << 'Z';
    return out.str();
}

[[nodiscard]] Timestamp timestamp_from_epoch_microseconds(std::string_view value) {
    std::int64_t microseconds{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                              microseconds);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw DatabaseError("database returned an invalid timestamp epoch");
    }
    return Timestamp{std::chrono::microseconds{microseconds}};
}

[[nodiscard]] std::string bool_text(bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] std::string int_text(std::int16_t value) {
    return std::to_string(value);
}

[[nodiscard]] SqlParameter optional_decimal_text(const std::optional<Decimal>& value) {
    if (!value) {
        return std::nullopt;
    }
    return value->to_string();
}

} // namespace

PostgresMarketDataRepository::PostgresMarketDataRepository(ConnectionConfig config)
    : PostgresMarketDataRepository(std::make_unique<PgConnection>(config)) {}

PostgresMarketDataRepository::PostgresMarketDataRepository(
    std::unique_ptr<PgConnection> connection)
    : connection_(std::move(connection)) {
    if (!connection_ || !connection_->is_open()) {
        throw std::invalid_argument("repository requires an open PostgreSQL connection");
    }
    prepare_statements();
}

void PostgresMarketDataRepository::prepare_statements() {
    connection_->prepare(
        "fincore_upsert_instrument",
        R"SQL(
            INSERT INTO fincore.instruments
                (symbol, name, asset_class, exchange, tick_size_decimals, is_active)
            VALUES ($1, $2, $3, $4, $5::smallint, $6::boolean)
            ON CONFLICT (symbol) DO UPDATE SET
                name = EXCLUDED.name,
                asset_class = EXCLUDED.asset_class,
                exchange = EXCLUDED.exchange,
                tick_size_decimals = EXCLUDED.tick_size_decimals,
                is_active = EXCLUDED.is_active
        )SQL",
        6);

    connection_->prepare(
        "fincore_upsert_data_source",
        R"SQL(
            INSERT INTO fincore.data_sources
                (code, display_name, base_url, is_active)
            VALUES ($1, $2, $3, $4::boolean)
            ON CONFLICT (code) DO UPDATE SET
                display_name = EXCLUDED.display_name,
                base_url = EXCLUDED.base_url,
                is_active = EXCLUDED.is_active
        )SQL",
        4);

    connection_->prepare(
        "fincore_insert_quote",
        R"SQL(
            INSERT INTO fincore.quotes
                (symbol, source, timestamp, price, open, high, low, volume, change_pct)
            VALUES
                ($1, $2, $3::timestamptz, $4::numeric, $5::numeric,
                 $6::numeric, $7::numeric, $8::numeric, $9::numeric)
            ON CONFLICT (symbol, source, timestamp) DO NOTHING
        )SQL",
        9);

    connection_->prepare(
        "fincore_insert_tick",
        R"SQL(
            INSERT INTO fincore.ticks
                (symbol, source, timestamp, trade_id, price, size, side)
            VALUES
                ($1, $2, $3::timestamptz, $4, $5::numeric, $6::numeric, $7::char(1))
            ON CONFLICT (source, symbol, trade_id)
                WHERE trade_id IS NOT NULL
            DO NOTHING
        )SQL",
        7);

    connection_->prepare(
        "fincore_insert_snapshot",
        R"SQL(
            INSERT INTO fincore.order_book_snapshots
                (symbol, source, snapshot_time, best_bid, best_ask,
                 imbalance, total_bid_vol, total_ask_vol)
            VALUES
                ($1, $2, $3::timestamptz, $4::numeric, $5::numeric,
                 $6::numeric, $7::numeric, $8::numeric)
            ON CONFLICT (symbol, source, snapshot_time) DO NOTHING
        )SQL",
        8);

    connection_->prepare(
        "fincore_insert_indicator",
        R"SQL(
            INSERT INTO fincore.technical_indicators
                (symbol, source, indicator_name, timestamp, value, parameters)
            VALUES
                ($1, $2, $3, $4::timestamptz, $5::numeric, $6::jsonb)
            ON CONFLICT (symbol, source, indicator_name, timestamp, parameters)
            DO NOTHING
        )SQL",
        6);

    connection_->prepare(
        "fincore_latest_quote",
        R"SQL(
            SELECT
                symbol,
                source,
                (extract(epoch FROM timestamp) * 1000000)::bigint,
                price::text,
                open::text,
                high::text,
                low::text,
                volume::text,
                change_pct::text
            FROM fincore.quotes
            WHERE symbol = $1 AND source = $2
            ORDER BY timestamp DESC
            LIMIT 1
        )SQL",
        2);
}

void PostgresMarketDataRepository::upsert_instrument(
    const domain::Instrument& instrument) {
    domain::validate(instrument);

    const auto result = connection_->execute_prepared(
        "fincore_upsert_instrument",
        {instrument.symbol,
         instrument.name,
         std::string(domain::to_string(instrument.asset_class)),
         instrument.exchange,
         int_text(instrument.tick_size_decimals),
         bool_text(instrument.is_active)});
    (void)result;
}

void PostgresMarketDataRepository::upsert_data_source(
    const domain::DataSource& source) {
    domain::validate(source);

    const auto result = connection_->execute_prepared(
        "fincore_upsert_data_source",
        {source.code,
         source.display_name,
         source.base_url ? SqlParameter{*source.base_url} : std::nullopt,
         bool_text(source.is_active)});
    (void)result;
}

InsertSummary PostgresMarketDataRepository::insert_quotes(
    std::span<const domain::Quote> quotes) {
    for (const auto& quote : quotes) {
        domain::validate(quote);
    }

    InsertSummary summary{quotes.size(), 0};
    if (quotes.empty()) {
        return summary;
    }

    PgTransaction transaction(*connection_);
    for (const auto& quote : quotes) {
        const auto result = connection_->execute_prepared(
            "fincore_insert_quote",
            {quote.symbol,
             quote.source,
             timestamp_to_utc_text(quote.timestamp),
             quote.price.to_string(),
             quote.open.to_string(),
             quote.high.to_string(),
             quote.low.to_string(),
             quote.volume.to_string(),
             optional_decimal_text(quote.change_pct)});
        summary.inserted += result.affected_rows();
    }
    transaction.commit();
    return summary;
}

InsertSummary PostgresMarketDataRepository::insert_ticks(
    std::span<const domain::Tick> ticks) {
    for (const auto& tick : ticks) {
        domain::validate(tick);
    }

    InsertSummary summary{ticks.size(), 0};
    if (ticks.empty()) {
        return summary;
    }

    PgTransaction transaction(*connection_);
    for (const auto& tick : ticks) {
        const auto result = connection_->execute_prepared(
            "fincore_insert_tick",
            {tick.symbol,
             tick.source,
             timestamp_to_utc_text(tick.timestamp),
             tick.trade_id ? SqlParameter{*tick.trade_id} : std::nullopt,
             tick.price.to_string(),
             tick.size.to_string(),
             std::string(1, domain::to_char(tick.side))});
        summary.inserted += result.affected_rows();
    }
    transaction.commit();
    return summary;
}

InsertSummary PostgresMarketDataRepository::insert_order_book_snapshots(
    std::span<const domain::OrderBookSnapshot> snapshots) {
    for (const auto& snapshot : snapshots) {
        domain::validate(snapshot);
    }

    InsertSummary summary{snapshots.size(), 0};
    if (snapshots.empty()) {
        return summary;
    }

    PgTransaction transaction(*connection_);
    for (const auto& snapshot : snapshots) {
        const auto result = connection_->execute_prepared(
            "fincore_insert_snapshot",
            {snapshot.symbol,
             snapshot.source,
             timestamp_to_utc_text(snapshot.snapshot_time),
             snapshot.best_bid.to_string(),
             snapshot.best_ask.to_string(),
             snapshot.imbalance.to_string(),
             snapshot.total_bid_volume.to_string(),
             snapshot.total_ask_volume.to_string()});
        summary.inserted += result.affected_rows();
    }
    transaction.commit();
    return summary;
}

InsertSummary PostgresMarketDataRepository::insert_technical_indicators(
    std::span<const domain::TechnicalIndicator> indicators) {
    for (const auto& indicator : indicators) {
        domain::validate(indicator);
    }

    InsertSummary summary{indicators.size(), 0};
    if (indicators.empty()) {
        return summary;
    }

    PgTransaction transaction(*connection_);
    for (const auto& indicator : indicators) {
        const auto result = connection_->execute_prepared(
            "fincore_insert_indicator",
            {indicator.symbol,
             indicator.source,
             indicator.indicator_name,
             timestamp_to_utc_text(indicator.timestamp),
             indicator.value.to_string(),
             indicator.parameters_json});
        summary.inserted += result.affected_rows();
    }
    transaction.commit();
    return summary;
}

std::optional<domain::Quote> PostgresMarketDataRepository::latest_quote(
    std::string_view symbol,
    std::string_view source) {
    const auto result = connection_->execute_prepared(
        "fincore_latest_quote",
        {std::string(symbol), std::string(source)});

    if (result.rows() == 0) {
        return std::nullopt;
    }

    domain::Quote quote;
    quote.symbol = std::string(result.value(0, 0));
    quote.source = std::string(result.value(0, 1));
    quote.timestamp = timestamp_from_epoch_microseconds(result.value(0, 2));
    quote.price = Decimal::from_string(result.value(0, 3));
    quote.open = Decimal::from_string(result.value(0, 4));
    quote.high = Decimal::from_string(result.value(0, 5));
    quote.low = Decimal::from_string(result.value(0, 6));
    quote.volume = Decimal::from_string(result.value(0, 7));
    if (!result.is_null(0, 8)) {
        quote.change_pct = Decimal::from_string(result.value(0, 8));
    }
    return quote;
}

} // namespace fincore::persistence::postgres
