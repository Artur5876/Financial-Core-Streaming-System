#include "api/alpha_vantage_client.hpp"
#include "cli/fincore_cli.hpp"
#include "cli/postgres_cli.hpp"
#include "storage/redis_client.hpp"
#include "storage/persistance/postgres/postgres_market_data_repository.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace fincore;

namespace {

    std::string env_or(const char* name, std::string fallback) {
        const char* value = std::getenv(name);
        return value ? std::string{value} : std::move(fallback);
    }

    int run_postgres_cli(int argc, char* argv[]) {
        std::vector<std::string> arguments;
        arguments.reserve(argc > 2 ? static_cast<std::size_t>(argc - 2) : 0);
        for (int i = 2; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }

        // Help must remain available even when PostgreSQL is not running.
        if (arguments.empty() || arguments[0] == "help" ||
            arguments[0] == "--help" || arguments[0] == "-h") {
            cli::PostgresCli::print_help(std::cout);
            return 0;
        }

        try {
            auto config = persistence::postgres::ConnectionConfig::from_environment();
            persistence::postgres::PostgresMarketDataRepository repository{config};
            cli::PostgresCli postgres_cli{repository, std::cout, std::cerr};
            return postgres_cli.run(arguments);
        } catch (const persistence::postgres::DatabaseError& error) {
            std::cerr << "database error";
            if (!error.sql_state().empty()) {
                std::cerr << " [SQLSTATE " << error.sql_state() << ']';
            }
            std::cerr << ": " << error.what() << '\n';
            return 1;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 2;
        }
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (std::string_view{argv[1]} == "postgres") {
            return run_postgres_cli(argc, argv);
        }
        std::cerr << "usage: fincore_app [postgres COMMAND [ARGUMENTS]]\n";
        return 2;
    }

    const std::string av_api_key = env_or("AV_API_KEY", "demo");
    const std::string redis_host = env_or("REDIS_HOST", "127.0.0.1");
    const int redis_port = std::stoi(env_or("REDIS_PORT", "6379"));
    const int redis_quote_ttl = std::stoi(
        env_or("REDIS_QUOTE_TTL_SECONDS", "60"));
    const int poll_seconds = std::stoi(env_or("POLL_SECONDS", "60"));

    std::vector<std::string> symbols{
        "AAPL",
        "MSFT",
        "GOOGL"
    };

    std::cout << "[FinCore] Starting Financial Core Streaming System\n"
              << "  Redis  : " << redis_host << ':' << redis_port << '\n'
              << "  Cache  : Redis quote TTL " << redis_quote_ttl << "s\n"
              << "  AV key : " << av_api_key.substr(0, 4) << "****\n"
              << "  Poll   : default " << poll_seconds << "s\n";

    AlphaVantageClient av_client{
        av_api_key,
        std::chrono::seconds{poll_seconds}
    };

    std::unique_ptr<RedisClient> redis;
    try {
        redis = std::make_unique<RedisClient>(
            redis_host,
            redis_port,
            std::chrono::seconds{redis_quote_ttl});
    } catch (const std::exception& error) {
        std::cerr << "[FinCore] Cannot connect to Redis: "
                  << error.what() << '\n';
        return 1;
    }

    std::unique_ptr<persistence::postgres::PostgresMarketDataRepository> postgres;
    try {
        auto database_config = persistence::postgres::ConnectionConfig::from_environment();
        postgres = std::make_unique<persistence::postgres::PostgresMarketDataRepository>(
            database_config);

        postgres->upsert_data_source({
            "ALPHA_VANTAGE",
            "Alpha Vantage",
            std::string{"https://www.alphavantage.co"},
            true});
        for (const auto& symbol : symbols) {
            postgres->upsert_instrument({
                symbol,
                symbol,
                domain::AssetClass::equity,
                "UNKNOWN",
                4,
                true});
        }

        std::cout << "  PostgreSQL: " << database_config.host << ':'
                  << database_config.port << '/' << database_config.database
                  << " [connected]\n";
    } catch (const persistence::postgres::DatabaseError& error) {
        std::cerr << "[FinCore] PostgreSQL disabled";
        if (!error.sql_state().empty()) {
            std::cerr << " [SQLSTATE " << error.sql_state() << ']';
        }
        std::cerr << ": " << error.what() << '\n';
        std::cerr << "[FinCore] Continuing with Redis and Alpha Vantage.\n";
    } catch (const std::exception& error) {
        std::cerr << "[FinCore] PostgreSQL disabled: invalid configuration: "
                  << error.what() << '\n';
        std::cerr << "[FinCore] Continuing with Redis and Alpha Vantage.\n";
    }

    cli::FinCoreCli app{
        av_client,
        *redis,
        postgres.get(),
        std::move(symbols),
        poll_seconds,
        std::cin,
        std::cout
    };

    return app.run();
}
