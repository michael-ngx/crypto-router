#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iostream>

namespace util {

class MarketDataLogger {
private:
    std::ofstream file_;
    std::mutex mutex_;

public:
    MarketDataLogger(const std::string& filename) {
        // ios::app ensures we append to the file if it already exists
        file_.open(filename, std::ios::app);
        if (file_.is_open()) {
            // Write CSV Header
            file_ << "timestamp_ms,venue,best_bid,bid_vol,best_ask,ask_vol\n";
        } else {
            std::cerr << "Failed to open ML logger file!\n";
        }
    }

    ~MarketDataLogger() {
        if (file_.is_open()) file_.close();
    }

    void log_snapshot(const std::string& venue, double b_price, double b_vol, double a_price, double a_vol) {
        if (!file_.is_open()) return;

        // Get current time in milliseconds
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::lock_guard<std::mutex> lock(mutex_);
        
        // Writing every piece of data separated by commas
        file_ << ms << "," 
            << venue << "," 
            << b_price << "," 
            << b_vol << "," 
            << a_price << "," 
            << a_vol << std::endl; // std::endl is the magic fix here
    }
};

} // namespace util