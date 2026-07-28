#include <chrono>
#include <iostream>
#include <iterator>
#include <unordered_map>

#include "storage/redis_client.hpp"
namespace fincore {
    RedisClient::RedisClient(
        const std::string& host,
        int port,
        std::chrono::seconds quote_ttl)
        : connection_string_("tcp://" + host + ":" + std::to_string(port)),
          quote_ttl_(quote_ttl) {
        if (quote_ttl_ <= std::chrono::seconds::zero()) {
            throw std::invalid_argument("RedisClient: quote TTL must be positive");
        }

        try {
            redis_ = std::make_unique<sw::redis::Redis>(connection_string_);
            redis_->ping();
            std::cout << "[Redis] Connected to " << host << ":" << port << "\n";
        } catch(const sw::redis::Error& e) {
            std::cerr << "[Redis] Connection failed: " << e.what() << "\n";
            throw;
        }
    }

//connectivity check
bool RedisClient::is_connected() const {
    try {
        redis_->ping();
        return true;
    } catch(...) {
        return false;
    }
}


bool RedisClient::store_quote(const Symbol& symbol, const Quote& quote) {
    try {
        //serialization of TimePoint to microseconds since epoch
        const int64_t ts_us = to_unix_us(quote.timestamp);

        std::unordered_map<std::string, std::string> fields = {
            {"price",      std::to_string(quote.price)},
            {"open",       std::to_string(quote.open)},
            {"high",       std::to_string(quote.high)},
            {"low",        std::to_string(quote.low)},
            {"volume",     std::to_string(quote.volume)},
            {"change_pct", std::to_string(quote.change_pct)},
            {"source",     quote.source},
            {"timestamp",  std::to_string(ts_us)},
        };

        redis_->hmset(quote_key(symbol), fields.begin(), fields.end());
        redis_->expire(quote_key(symbol), quote_ttl_);

        //Append a compact record to the history list (newest first)
        const std::string history_entry =
            std::to_string(ts_us) + ":" +
            std::to_string(quote.price) + ":" +
            std::to_string(quote.volume);

        redis_->lpush(quote_history_key(symbol), history_entry);
        redis_->ltrim(quote_history_key(symbol), 0, 999);   //keep last 1000

        return true;
    } catch (const sw::redis::Error& e) {
        std::cerr << "[Redis] store_quote error: " << e.what() << "\n";
        return false;
    }
}

std::optional<Quote> RedisClient::get_quote(const Symbol& symbol) {
    try{
        std::unordered_map<std::string, std::string> fields;
        redis_->hgetall(quote_key(symbol), std::inserter(fields, fields.begin()));

        if (fields.empty()) {
            return std::nullopt;
        }

        //Safe parser for double field (return 0.0 if symbol is missing)
        auto get_double = [&] (const std::string& key) -> double {
            auto it = fields.find(key);
            if (it == fields.end()) return 0.0;
            try{ return std::stod(it->second); }
            catch(...) { return 0.0; }
        };

        auto get_uint64 = [&] (const std::string& key) -> std::uint64_t {
            auto it = fields.find(key);
            if (it == fields.end()) return 0ULL;
            try{ return std::stoull(it->second); }
            catch(...) { return 0ULL; }
        };

        Quote q;
        q.symbol        = symbol;
        q.price         = get_double("price");
        q.open          = get_double("open");
        q.high          = get_double("high");
        q.low           = get_double("low");
        q.volume        = get_uint64("volume");
        q.change_pct    = get_double("change_pct");


        //if 'source' key is available
        if (auto it = fields.find("source"); it != fields.end())
            q.source = it->second;

        //TimePoint desirialization from microsdeconds
        if (auto it = fields.find("timestamp"); it != fields.end()) {
            try {
                int64_t us = std::stoll(it->second);
                q.timestamp = TimePoint(std::chrono::microseconds(us));
            } catch(...) { std::cout << "Timestamp is default-constructed attribute\n"; }
        }

        return q.is_valid() ? std::optional<Quote>{q} : std::nullopt;
    } catch(const std::exception& e) {
        std::cerr << "[Redis] get_quote error: " << e.what() << "\n";
        return std::nullopt;
    }
}

//Tick struct(for high-frequency stream)
bool RedisClient::store_tick(const Tick& tick) {
    try{
        const int64_t ts_us = to_unix_us(tick.timestamp);
        std::unordered_map<std::string, std::string> fields = {
            {"symbol",      tick.symbol},
            {"price",       std::to_string(tick.price)},
            {"size",        std::to_string(tick.size)},
            {"side",        std::string(to_string(tick.side))},
            {"timestamp",   std::to_string(ts_us)}
        };

        //Redis stream append
        redis_->xadd(tick_stream_key(tick.symbol), "*", fields.begin(), fields.end());

        //Cashe the very latest price for quick reads
        redis_->set("latest_tick:" + tick.symbol,
                  std::to_string(tick.price) + ":" + std::to_string(tick.size));

        return true;
    } catch(const sw::redis::Error& e) {
        std::cerr << "[Redis] store_tick error: " << e.what() << "\n";
        return false;
    }

}
bool RedisClient::update_order_book(const Symbol& symbol,
                                    const std::map<Price, Volume>& bids,
                                    const std::map<Price, Volume>& asks) {
    try{
        const std::string bids_key = "order_book:" + symbol + ":bids";
        const std::string asks_key = "order_book:" + symbol + ":asks";

        redis_->del(bids_key);
        redis_->del(asks_key);

        for (const auto& [price, vol] : bids)
            redis_->hset(bids_key, std::to_string(price), std::to_string(vol));

        for (const auto& [price, vol] : asks)
            redis_->hset(asks_key, std::to_string(price), std::to_string(vol));

        const int64_t now_us = to_unix_us(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()));
        redis_->set("order_book:" + symbol + ":timestamp", std::to_string(now_us));
        return true;

    } catch (const sw::redis::Error& e) {
        std::cerr << "[Redis] update_order_book error: " << e.what() << "\n";
        return false;
    }
}

//helper-functions
std::string RedisClient::quote_key(const Symbol& symbol) const {
    return "quote:" +symbol;
}

std::string RedisClient::quote_history_key(const Symbol& symbol) const {
    return "quote_history:" + symbol;
}

std::string RedisClient::tick_stream_key(const Symbol& symbol) const {
        return "ticks:" + symbol;
    }
}
