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
    }

    //Connection Config Data selection
    struct ConnectionConfig {
        std::string host{"127.0.0.1"};
        std::string port{"5432"};
        std::string database{"fincore"};
        std::string user{"fincore_app"};
        std::string password; // will be configured doring runtime
        std::string application_name{"fincore-cpp"};
        int connection_timeout_seconds{5};

        [[nodiscard]] static ConnectionConfig from_environment();
    };

    class PGResult final {
        public:
            explicit PGResult(PGResult* result = nullptr) noexcept;
            ~PGResult();

            PGResult(const PGResult&) = delete;
            PGResult operator=(const PGResult&) = delete;

            PGResult(PGResult&& other) noexcept;
            PGResult& operator=(PGResult&& other) noexcept;

            [[nodiscard]] int row() const noexcept;
            [[nodiscard]] int column() const noexcept;
            [[nodiscard]] bool is_null(int row, int column) const;
            [[nodiscard]] std::string_view value(int row, int column) const;
            [[nodiscard]] std::size_t affected_rows() const;

        private:
            PGResult* result_{};
    };

    using SqlParameter = std::optional<std::string>;

    class PgConnection final {
        public:
            explicit PgConnection(const ConnectionConfig& config);
            ~PgConnection();

            PgConnection(const PgConnection&) = delete;
            PgConnection operator=(const PgConnection&) = delete;

            PgConnection(PgConnection&& other) noexcept;
            PgConnection& operator=(PgConnection&& other) noexcept;

            [[nodiscard]] PGResult execute(std::string_view sql);
            [[nodiscard]] PGResult execute_params(
                    std::string_view sql,
                    const std::vector<SqlParameter>& parameters);

            void prepare(
                        std::string_view name,
                        std::string_view sql,
                        int parameter_count);

            [[nodiscard]] PGResult execute_prepare(
                std::string_view name,
                const std::vector<SqlParameter>& parameters);

            [[nodiscard]] bool is_open() const noexcept;

        private:
            void close() noexcept;
            [[nodiscard]] PGResult check_result(PGResult* result) const;

            PGconn* connection_{};
    };

    class PgTransaction final {
        public:
            explicit PgTransaction(PgConnection& connection);
            ~PgTransaction();

            PgTransaction(const PgTransaction&) = delete;
            PgTransaction operator=(const PgTransaction&) = delete;

            void commit();

        private:
            PgConnection* connection_{};
            bool active_{true};
    };
}
