#include "storage/cli_postgres.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace fincore {
namespace {

std::string timestamp_text(TimePoint timestamp) {
    if (timestamp == TimePoint{}) {
        timestamp = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now());
    }

    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        timestamp - seconds).count();
    const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
    gmtime_r(&raw, &utc);

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(6) << std::setfill('0') << micros << "+00";
    return out.str();
}

std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

} // namespace

void CliPostgres::ConnectionDeleter::operator()(PGconn* connection) const noexcept {
    if (connection) {
        PQfinish(connection);
    }
}

CliPostgres::CliPostgres(const PostgresConfig& config) {
    const std::string port = std::to_string(config.port);
    const std::array<const char*, 8> keywords{
        "host", "port", "dbname", "user", "password", "sslmode", "application_name", nullptr};
    const std::array<const char*, 8> values{
        config.host.c_str(), port.c_str(), config.database.c_str(), config.user.c_str(),
        config.password.c_str(), config.ssl_mode.c_str(), "fincore_cli", nullptr};
    connection_.reset(PQconnectdbParams(keywords.data(), values.data(), 0));
    if (!is_connected()) {
        set_error(connection_ ? PQerrorMessage(connection_.get()) : "connection allocation failed");
    }
}

bool CliPostgres::is_connected() const {
    return connection_ && PQstatus(connection_.get()) == CONNECTION_OK;
}

const std::string& CliPostgres::last_error() const noexcept {
    return last_error_;
}

void CliPostgres::set_error(std::string error) {
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) {
        error.pop_back();
    }
    last_error_ = std::move(error);
}

bool CliPostgres::execute(const char* sql, const char* const* values, int count) {
    if (!is_connected()) {
        set_error("PostgreSQL is disconnected");
        return false;
    }
    std::unique_ptr<PGresult, decltype(&PQclear)> result(
        PQexecParams(connection_.get(), sql, count, nullptr, values, nullptr, nullptr, 0), &PQclear);
    if (!result || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        set_error(PQerrorMessage(connection_.get()));
        return false;
    }
    last_error_.clear();
    return true;
}

bool CliPostgres::ensure_instrument(const Symbol& symbol) {
    const char* values[]{symbol.c_str()};
    return execute(
        "INSERT INTO instruments (symbol,name,asset_class,exchange) "
        "VALUES ($1,$1,'EQUITY','UNKNOWN') ON CONFLICT (symbol) DO NOTHING",
        values, 1);
}

bool CliPostgres::store_quote(const Quote& quote) {
    if (!ensure_instrument(quote.symbol)) {
        return false;
    }
    const std::string price = number(quote.price);
    const std::string open = number(quote.open);
    const std::string high = number(quote.high);
    const std::string low = number(quote.low);
    const std::string volume = std::to_string(quote.volume);
    const std::string change = number(quote.change_pct);
    const std::string timestamp = timestamp_text(quote.timestamp);
    const char* values[]{quote.symbol.c_str(), price.c_str(), open.c_str(), high.c_str(),
                         low.c_str(), volume.c_str(), change.c_str(), quote.source.c_str(),
                         timestamp.c_str()};
    return execute(
        "INSERT INTO quotes (symbol,price,open,high,low,volume,change_pct,source,timestamp) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9)", values, 9);
}

bool CliPostgres::store_snapshot(const OrderBookSnapshot& snapshot) {
    if (!ensure_instrument(snapshot.symbol)) {
        return false;
    }
    const std::string bid = number(snapshot.best_bid);
    const std::string ask = number(snapshot.best_ask);
    const std::string imbalance = number(snapshot.imbalance);
    const std::string bid_volume = std::to_string(snapshot.total_bid_vol);
    const std::string ask_volume = std::to_string(snapshot.total_ask_vol);
    const std::string timestamp = timestamp_text(snapshot.snapshot_time);
    const char* values[]{snapshot.symbol.c_str(), bid.c_str(), ask.c_str(), imbalance.c_str(),
                         bid_volume.c_str(), ask_volume.c_str(), timestamp.c_str()};
    return execute(
        "INSERT INTO order_book_snapshots "
        "(symbol,best_bid,best_ask,imbalance,total_bid_vol,total_ask_vol,snapshot_time) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7)", values, 7);
}

} // namespace fincore
