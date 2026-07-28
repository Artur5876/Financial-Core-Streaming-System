#include "api/alpha_vantage_client.hpp"
#include "cli/fincore_cli.hpp"
#include "storage/redis_client.hpp"

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

} // namespace

int main() {
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

    cli::FinCoreCli app{
        av_client,
        *redis,
        std::move(symbols),
        poll_seconds,
        std::cin,
        std::cout
    };

    return app.run();
}
