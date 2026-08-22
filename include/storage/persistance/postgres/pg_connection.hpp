#pragma once

#include <libpq-fe.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fincore::persistence::postgres {
    // CLass for representing not impossible database with information about
    // sql-state and its message
    // ALL of it can occure in runtime (when user running queries)
    class DatabaseError : public std::runtime_error {
        public:
            DatabaseError(std::string message, std::string sql_state = {});

            [[nodiscard]] const std::string& sql_state() const noexcept { return sql_state_;}
        private:
            std::string sql_state_;
    };

    //Connection Config Data selection
    struct ConnectionConfig {
        std::string host{"127.0.0.1"};
        std::string port{"5432"};
        std::string database{"fincore"};
        std::string user{"fincore_app"};
        std::string password; // will be configured doring runtime
        std::string application_name{"fincore-cpp"};
        int connect_timeout_seconds{5};

        [[nodiscard]] static ConnectionConfig from_environment();
    };

    //RAII for PGresult
    //libpq return raw PGresult* values that must be released using PQclear
    class PgResult final {
        public:
            explicit PgResult(PGresult* result = nullptr) noexcept;
            ~PgResult();

            //copying is disabled
            PgResult(const PgResult&) = delete;
            // Moving is disabled
            PgResult& operator=(const PgResult&) = delete;

            PgResult(PgResult&& other) noexcept;
            PgResult& operator=(PgResult&& other) noexcept;

            [[nodiscard]] int rows() const noexcept;
            [[nodiscard]] int columns() const noexcept;
            [[nodiscard]] bool is_null(int row, int column) const;
            [[nodiscard]] std::string_view value(int row, int column) const;
            [[nodiscard]] std::size_t affected_rows() const;

        private:
            PGresult* result_{};
    };

    using SqlParameter = std::optional<std::string>;

    class PgConnection final {
        public:
            explicit PgConnection(const ConnectionConfig& config);
            ~PgConnection();

            PgConnection(const PgConnection&) = delete;
            PgConnection& operator=(const PgConnection&) = delete;

            PgConnection(PgConnection&& other) noexcept;
            PgConnection& operator=(PgConnection&& other) noexcept;

            [[nodiscard]] PgResult execute(std::string_view sql);
            [[nodiscard]] PgResult execute_params(
                    std::string_view sql,
                    const std::vector<SqlParameter>& parameters);

            void prepare(
                        std::string_view name,
                        std::string_view sql,
                        int parameter_count);

            [[nodiscard]] PgResult execute_prepared(
                std::string_view name,
                const std::vector<SqlParameter>& parameters);

            [[nodiscard]] bool is_open() const noexcept;

        private:
            void close() noexcept;
            [[nodiscard]] PgResult check_result(PGresult* result) const;

            PGconn* connection_{};
    };

    class PgTransaction final {
        public:
            explicit PgTransaction(PgConnection& connection);
            ~PgTransaction();

            PgTransaction(const PgTransaction&) = delete;
            PgTransaction& operator=(const PgTransaction&) = delete;

            void commit();

       private:
            PgConnection* connection_{};
            bool active_{true};
    };
}
