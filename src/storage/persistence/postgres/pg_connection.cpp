#include "storage/persistance/postgres/pg_connection.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <utility>

namespace fincore::persistence::postgres {
namespace {

[[nodiscard]] std::string environment_or(std::string_view name,
                                         std::string fallback = {}) {
    if (const char* value = std::getenv(std::string(name).c_str()); value != nullptr) {
        return value;
    }
    return fallback;
}

[[nodiscard]] int environment_int_or(std::string_view name, int fallback) {
    const auto text = environment_or(name);
    if (text.empty()) {
        return fallback;
    }

    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0) {
        throw std::invalid_argument("invalid positive integer in environment variable " +
                                    std::string(name));
    }
    return value;
}

[[nodiscard]] std::vector<const char*> parameter_values(
    const std::vector<SqlParameter>& parameters) {
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        values.push_back(parameter ? parameter->c_str() : nullptr);
    }
    return values;
}

[[nodiscard]] std::string result_error(PGconn* connection, PGresult* result) {
    if (result != nullptr) {
        if (const char* message = PQresultErrorMessage(result);
            message != nullptr && *message != '\0') {
            return message;
        }
    }
    if (connection != nullptr) {
        if (const char* message = PQerrorMessage(connection);
            message != nullptr && *message != '\0') {
            return message;
        }
    }
    return "unknown PostgreSQL error";
}

[[nodiscard]] std::string result_sql_state(PGresult* result) {
    if (result == nullptr) {
        return {};
    }
    if (const char* state = PQresultErrorField(result, PG_DIAG_SQLSTATE); state != nullptr) {
        return state;
    }
    return {};
}

} // namespace

DatabaseError::DatabaseError(std::string message, std::string sql_state)
    : std::runtime_error(std::move(message)), sql_state_(std::move(sql_state)) {}

ConnectionConfig ConnectionConfig::from_environment() {
    ConnectionConfig config;
    config.host = environment_or("FINCORE_DB_HOST", config.host);
    config.port = environment_or("FINCORE_DB_PORT", config.port);
    config.database = environment_or("FINCORE_DB_NAME", config.database);
    config.user = environment_or("FINCORE_DB_USER", config.user);
    config.password = environment_or("FINCORE_DB_PASSWORD");
    config.application_name = environment_or("FINCORE_DB_APPLICATION_NAME",
                                             config.application_name);
    config.connect_timeout_seconds = environment_int_or(
        "FINCORE_DB_CONNECT_TIMEOUT", config.connect_timeout_seconds);
    return config;
}

PgResult::PgResult(PGresult* result) noexcept : result_(result) {}

PgResult::~PgResult() {
    if (result_ != nullptr) {
        PQclear(result_);
    }
}

PgResult::PgResult(PgResult&& other) noexcept
    : result_(std::exchange(other.result_, nullptr)) {}

PgResult& PgResult::operator=(PgResult&& other) noexcept {
    if (this != &other) {
        if (result_ != nullptr) {
            PQclear(result_);
        }
        result_ = std::exchange(other.result_, nullptr);
    }
    return *this;
}

int PgResult::rows() const noexcept {
    return result_ == nullptr ? 0 : PQntuples(result_);
}

int PgResult::columns() const noexcept {
    return result_ == nullptr ? 0 : PQnfields(result_);
}

bool PgResult::is_null(int row, int column) const {
    if (result_ == nullptr || row < 0 || row >= rows() ||
        column < 0 || column >= columns()) {
        throw std::out_of_range("PostgreSQL result coordinate is out of range");
    }
    return PQgetisnull(result_, row, column) != 0;
}

std::string_view PgResult::value(int row, int column) const {
    if (is_null(row, column)) {
        throw std::logic_error("attempted to read a SQL NULL as text");
    }
    const char* text = PQgetvalue(result_, row, column);
    const int length = PQgetlength(result_, row, column);
    return {text, static_cast<std::size_t>(length)};
}

std::size_t PgResult::affected_rows() const {
    if (result_ == nullptr) {
        return 0;
    }

    const char* text = PQcmdTuples(result_);
    if (text == nullptr || *text == '\0') {
        return 0;
    }

    std::size_t value{};
    const std::string_view view{text};
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (error != std::errc{} || end != view.data() + view.size()) {
        throw DatabaseError("PostgreSQL returned an invalid affected-row count");
    }
    return value;
}

PgConnection::PgConnection(const ConnectionConfig& config) {
    const std::string timeout = std::to_string(config.connect_timeout_seconds);
    const std::array<const char*, 8> keywords{
        "host", "port", "dbname", "user", "password", "application_name",
        "connect_timeout", nullptr};
    const std::array<const char*, 8> values{
        config.host.c_str(), config.port.c_str(), config.database.c_str(),
        config.user.c_str(), config.password.c_str(), config.application_name.c_str(),
        timeout.c_str(), nullptr};

    connection_ = PQconnectdbParams(keywords.data(), values.data(), 0);
    if (connection_ == nullptr) {
        throw DatabaseError("libpq could not allocate a PostgreSQL connection");
    }
    if (PQstatus(connection_) != CONNECTION_OK) {
        const std::string message = PQerrorMessage(connection_);
        close();
        throw DatabaseError(message);
    }

    try {
        const auto result = execute("SET TIME ZONE 'UTC'");
        (void)result;
    } catch (...) {
        // A destructor is not run when a constructor throws, so release the
        // libpq handle explicitly before propagating the setup failure.
        close();
        throw;
    }
}

PgConnection::~PgConnection() {
    close();
}

PgConnection::PgConnection(PgConnection&& other) noexcept
    : connection_(std::exchange(other.connection_, nullptr)) {}

PgConnection& PgConnection::operator=(PgConnection&& other) noexcept {
    if (this != &other) {
        close();
        connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
}

PgResult PgConnection::execute(std::string_view sql) {
    if (!is_open()) {
        throw DatabaseError("cannot execute SQL on a closed PostgreSQL connection");
    }
    const std::string statement(sql);
    return check_result(PQexec(connection_, statement.c_str()));
}

PgResult PgConnection::execute_params(
    std::string_view sql,
    const std::vector<SqlParameter>& parameters) {
    if (!is_open()) {
        throw DatabaseError("cannot execute SQL on a closed PostgreSQL connection");
    }
    const std::string statement(sql);
    const auto values = parameter_values(parameters);
    return check_result(PQexecParams(connection_, statement.c_str(),
                                     static_cast<int>(parameters.size()), nullptr,
                                     values.data(), nullptr, nullptr, 0));
}

void PgConnection::prepare(std::string_view name,
                           std::string_view sql,
                           int parameter_count) {
    if (!is_open()) {
        throw DatabaseError("cannot prepare SQL on a closed PostgreSQL connection");
    }
    const std::string statement_name(name);
    const std::string statement(sql);
    const auto result = check_result(PQprepare(connection_, statement_name.c_str(),
                                               statement.c_str(), parameter_count,
                                               nullptr));
    (void)result;
}

PgResult PgConnection::execute_prepared(
    std::string_view name,
    const std::vector<SqlParameter>& parameters) {
    if (!is_open()) {
        throw DatabaseError("cannot execute prepared SQL on a closed PostgreSQL connection");
    }
    const std::string statement_name(name);
    const auto values = parameter_values(parameters);
    return check_result(PQexecPrepared(connection_, statement_name.c_str(),
                                       static_cast<int>(parameters.size()),
                                       values.data(), nullptr, nullptr, 0));
}

bool PgConnection::is_open() const noexcept {
    return connection_ != nullptr && PQstatus(connection_) == CONNECTION_OK;
}

PgResult PgConnection::check_result(PGresult* result) const {
    if (result == nullptr) {
        throw DatabaseError(result_error(connection_, nullptr));
    }

    const ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        const std::string message = result_error(connection_, result);
        const std::string sql_state = result_sql_state(result);
        PQclear(result);
        throw DatabaseError(message, sql_state);
    }
    return PgResult(result);
}

void PgConnection::close() noexcept {
    if (connection_ != nullptr) {
        PQfinish(connection_);
        connection_ = nullptr;
    }
}

PgTransaction::PgTransaction(PgConnection& connection) : connection_(&connection) {
    const auto result = connection_->execute("BEGIN");
    (void)result;
}

PgTransaction::~PgTransaction() {
    if (active_ && connection_ != nullptr) {
        try {
            const auto result = connection_->execute("ROLLBACK");
            (void)result;
        } catch (...) {
            // Destructors must not throw. The original operation's exception wins.
        }
    }
}

void PgTransaction::commit() {
    if (!active_ || connection_ == nullptr) {
        throw std::logic_error("transaction is no longer active");
    }
    const auto result = connection_->execute("COMMIT");
    (void)result;
    active_ = false;
}

} // namespace fincore::persistence::postgres
