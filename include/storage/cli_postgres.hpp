#pragma once

#include "core/types.hpp"

#include <libpq-fe.h>

#include <memory>
#include <string>

namespace fincore {

struct PostgresConfig {
    std::string host{"127.0.0.1"};
    int port{5432};
    std::string database{"fincore"};
    std::string user{"fincore_app"};
    std::string password;
    std::string ssl_mode{"prefer"};
};

// Narrow PostgreSQL adapter for the interactive ingestion path.
class CliPostgres {
public:
    explicit CliPostgres(const PostgresConfig& config);

    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] const std::string& last_error() const noexcept;

    bool store_quote(const Quote& quote);
    bool store_snapshot(const OrderBookSnapshot& snapshot);

private:
    struct ConnectionDeleter {
        void operator()(PGconn* connection) const noexcept;
    };

    bool ensure_instrument(const Symbol& symbol);
    bool execute(const char* sql, const char* const* values, int count);
    void set_error(std::string error);

    std::unique_ptr<PGconn, ConnectionDeleter> connection_;
    std::string last_error_;
};

} // namespace fincore
