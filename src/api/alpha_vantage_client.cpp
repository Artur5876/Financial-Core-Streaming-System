#include "api/alpha_vantage_client.hpp"

#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iomanip>

namespace fincore {
    //---libcurl write callback---
    //Called repeatedly by curl as data arrives;
    //Appends chunks to a std::string

    static size_t curl_write_cb(const char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buf = static_cast<std::string*>(userdata);
        buf->append(ptr, nmemb);
        return nmemb;
    }

    AlphaVantageClient::AlphaVantageClient(std::string api_key,
                                       std::chrono::seconds cache_ttl,
                                       FetchFunction fetch_function)
    : api_key_(std::move(api_key))
    , cache_ttl_(cache_ttl)
    , fetch_function_(fetch_function)
    {
        if (api_key_.empty())
            throw std::invalid_argument("AlphaVantageClient: api_key must not be empty");

        // curl_global_init is not thread-safe; call once at program start.
        // We call it here for simplicity - in a multi-threaded app move it to main().
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }


    AlphaVantageClient::~AlphaVantageClient() {
        curl_global_cleanup();
    }


    //---Public methods---
    std::optional<Quote>
    AlphaVantageClient::get_quote_fresh(const Symbol& symbol)
    {
        last_was_cached_ = false;
        if (symbol.empty()) {
            return std::nullopt;
        }
        const std::string json =
            fetch_function_
                ? fetch_function_(symbol)
                : fetch_json(symbol);
        if (json.empty()) {
            return std::nullopt;
        }
        auto result = parse_quote(json, symbol);
        if (!result) {
            return std::nullopt;
        }
        cache_[symbol] = CacheEntry{
            *result,
            std::chrono::steady_clock::now()
        };
        return result;
    }

    std::optional<Quote> AlphaVantageClient::get_quote(const Symbol& symbol) {
        last_was_cached_ = false;

        if(symbol.empty()) {
            std::cerr << "[AlphaVantage] symbol must not be empty!\n";
            return std::nullopt;
        }

        //Check cache first
        auto it = cache_.find(symbol);

        if (it != cache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.fetched_at;
            if (age < cache_ttl_) {
                last_was_cached_ = true;
                return it->second.quote;
            }
        }

        //if its not in cache
        return get_quote_fresh(symbol);
    }



    //---Network---
    std::string AlphaVantageClient::fetch_json(const Symbol& symbol) {
        const std::string url =
            "https://www.alphavantage.co/query"
            "?function=GLOBAL_QUOTE"
            "&symbol=" + symbol +
            "&apikey=" + api_key_;
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "[AlphaVantage] curl_easy_init failed \n";
            return {};
        }

        //String-body with network connection status(from CURL)
        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L); //10 seconds
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "fincore/1.0");

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[AlphaVantage] curl error: " << curl_easy_strerror(res) << "\n";
            return {};
        }
        if (http_code != 200) {
            std::cerr << "[AlphaVantage] HTTP " << http_code << " for symbol " << symbol << "\n";
            return {};
        }

        //if alphavan returns HTTP 200 on API errors, we are going to check the body
        if (body.find("Error Message") != std::string::npos ||
    body.find("\"Note\"")      != std::string::npos ||   // rate-limit note
    body.find("\"Information\"") != std::string::npos)   // premium-only note
        {
            std::cerr << "[AlphaVantage] API error for " << symbol << ": " << body << "\n";
            return {};
        }

        return body;

    }

    //---Json field helpers---
    bool AlphaVantageClient::extract_double(const std::string& json,
                                            const std::string& key, double& out)
    {
        std::string raw;
        if (!extract_string(json, key, raw)) return false;
        try { out = std::stod(raw); return true; }
        catch (...) { return false; }
    }

    bool AlphaVantageClient::extract_string(const std::string& json,
                                            const std::string& key, std::string& out)
    {
        const auto pos = json.find(key);
        if (pos == std::string::npos) return false;

        const auto start = pos + key.size();
        const auto end   = json.find('"', start);
        if (end == std::string::npos) return false;

        out = json.substr(start, end - start);
        return true;
    }

    //---Parsing---
    std::optional<Quote> AlphaVantageClient::parse_quote(const std::string& json, const Symbol& symbol) {

        //"01. symbol", "02. open", "03. high", "04. low", "05. price",
        //"06. volume", "07. latest trading day", "08. previous close",
        //"09. change", "10. change percent"

        Quote q;
        q.symbol = symbol;
        //q.source = "ALPHA_VANTAGE";

        bool ok = true;
        ok &= extract_double(json, "\"05. price\": \"",  q.price);
        ok &= extract_double(json, "\"02. open\": \"",   q.open);
        ok &= extract_double(json, "\"03. high\": \"",   q.high);
        ok &= extract_double(json, "\"04. low\": \"",    q.low);


        if (!ok) {
            std::cerr << "[AlphaVantage] Failed to parse price fields for " << symbol << "\n";
            return std::nullopt;
        }

        //volume
        std::string vol_str;
        if (extract_string(json, "\"06. volume\": \"", vol_str)) {
            try { q.volume = static_cast<Volume>(std::stoll(vol_str)); }
            catch (...) { q.volume = 0; }
        }

        //change_pct validation
        std::string pct_str;
        if (extract_string(json, "\"10. change percent\": \"", pct_str)) {
            if (!pct_str.empty() && pct_str.back() == '%') pct_str.pop_back();
            try { q.change_pct = std::stod(pct_str); }
            catch (...) { q.change_pct = 0.0; }
        }


        //Convert the date string to TimePoint at midnight UTC
        std::string date_str;
        if (extract_string(json, "\"07. latest trading day\": \"", date_str)) {
            std::tm tm{};
            std::istringstream ss(date_str);
            ss >> std::setw(4) >> tm.tm_year;
            if (!ss.fail()) {
                tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
                std::time_t t = std::mktime(&tm);   // converts to local time (see note)
                auto sys = std::chrono::system_clock::from_time_t(t);
                q.timestamp = TimePoint{
                    std::chrono::duration_cast<std::chrono::microseconds>(sys.time_since_epoch())
                };
            }
        }

        // if (!q.is_valid()) {
        //     std::cerr << "[AlphaVantage] Parsed quote failed validation for " << symbol << "\n";
        //     return std::nullopt;
        // }


        return q;
    }







}
