#pragma once
#include "core/types.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>

namespace fincore {

class RedisClient {
public:
    explicit RedisClient(
        const std::string& host = "127.0.0.1",
        int port = 6379,
        std::chrono::seconds quote_ttl = std::chrono::seconds{60});

    //modern: Returns bool for success/failure
    bool store_quote(const Symbol& symbol, const Quote& quote);

    //rturns optional (C++17)
    std::optional<Quote> get_quote(const Symbol& symbol);

    bool store_tick(const Tick& tick);


    bool update_order_book(const Symbol& symbol,
                           const std::map<Price, Volume>& bids,
                           const std::map<Price, Volume>& asks);

    //connection health
    bool is_connected() const;

private:
    std::unique_ptr<sw::redis::Redis> redis_;
    std::string connection_string_;
    std::chrono::seconds quote_ttl_;

    // Helper to create Redis key names consistently
    std::string quote_key(const Symbol& symbol) const;
    std::string quote_history_key(const Symbol& symbol) const;
    std::string tick_stream_key(const Symbol& symbol) const;
};

}
