#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ui/master_feed.hpp"
#include "venues/venue_api.hpp"
#include "venues/venue_factory.hpp"

class FeedManager {
public:
    // Represents a venue, and its API instance for use in FeedManager
    struct VenueRuntime {
        std::string name;
        const VenueFactory* factory{nullptr};
        std::unique_ptr<IVenueApi> api;
    };

    // FeedManager config option
    struct Options {
        std::chrono::seconds idle_timeout{std::chrono::minutes(3)};
        std::chrono::seconds sweep_interval{std::chrono::seconds(15)};
        std::vector<std::string> hot_pairs;
        bool prewarm_all{false};
    };

    FeedManager(std::vector<VenueRuntime> venues,
                std::vector<std::string> canonical_pairs)
        : FeedManager(std::move(venues), std::move(canonical_pairs), Options{}) {}

    FeedManager(std::vector<VenueRuntime> venues,
                std::vector<std::string> canonical_pairs,
                Options opts)
        : venues_(std::move(venues)),
          canonical_pairs_(std::move(canonical_pairs)),
          opts_(std::move(opts)) {
        build_support_index();

        for (const auto& pair : opts_.hot_pairs) {
            if (support_index_.find(pair) != support_index_.end()) {
                hot_pairs_.insert(pair);
            } else {
                std::cout << "[feed] Requested hot pair '" << pair
                          << "' is not supported and will be ignored."
                          << std::endl;
            }
        }
        // Config option: load all supported pairs
        if (opts_.prewarm_all) {
            for (const auto& pair : supported_pairs_) {
                hot_pairs_.insert(pair);
            }
        }

        if (can_sweep()) {
            running_.store(true, std::memory_order_relaxed);
            sweeper_ = std::thread([this] { sweep_loop(); });
        }
    }

    ~FeedManager() { shutdown(); }

    std::shared_ptr<UIMasterFeed> get_or_subscribe(const std::string& symbol) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(m_);

        auto it = entries_.find(symbol);
        if (it != entries_.end()) {
            it->second.last_access = now;
            if (hot_pairs_.count(symbol)) {
                it->second.pinned = true;
            }
            return it->second.ui;
        }

        auto sit = support_index_.find(symbol);
        if (sit == support_index_.end() || sit->second.empty()) {
            return nullptr;
        }

        Entry entry;
        entry.symbol = symbol;
        entry.ui = std::make_shared<UIMasterFeed>(symbol);
        entry.last_access = now;
        entry.pinned = hot_pairs_.count(symbol) > 0;

        bool registered = false;
        for (auto idx : sit->second) {
            if (idx >= venues_.size()) continue;
            const auto& venue = venues_[idx];
            if (!venue.factory) continue;

            auto feed = venue.factory->make_feed
                ? venue.factory->make_feed(symbol)
                : nullptr;
            if (!feed) {
                std::cerr << "[setup] Venue '" << venue.name
                          << "' failed to create feed; skipping."
                          << std::endl;
                continue;
            }

            const std::string venue_symbol =
                venue.factory->to_venue_symbol
                    ? venue.factory->to_venue_symbol(symbol)
                    : symbol;
            feed->start_ws(venue_symbol, 443);
            entry.ui->add_feed(feed);
            entry.feeds.push_back(feed);
            registered = true;
        }

        if (!registered) {
            return nullptr;
        }

        if (entry.pinned) {
            std::cout << "[feed] Pre-warmed pair '" << symbol
                      << "' subscribed and running."
                      << std::endl;
        } else {
            std::cout << "[feed] On-click load: non-prewarmed pair '" << symbol
                      << "' subscribed and running."
                      << std::endl;
        }

        auto [inserted, _] = entries_.emplace(symbol, std::move(entry));
        return inserted->second.ui;
    }

    std::vector<std::string> list_supported_pairs() const {
        return supported_pairs_;
    }

    void start_hot() {
        for (const auto& pair : hot_pairs_) {
            (void)get_or_subscribe(pair);
        }
    }

    void start_all_supported() {
        std::vector<std::string> all = supported_pairs_;
        std::sort(all.begin(), all.end());

        for (const auto& pair : all) {
            hot_pairs_.insert(pair);
            (void)get_or_subscribe(pair);
        }
    }

    void shutdown() {
        const bool was_running = running_.exchange(false, std::memory_order_relaxed);
        if (was_running && sweeper_.joinable()) {
            sweeper_.join();
        } else if (sweeper_.joinable()) {
            sweeper_.join();
        }

        std::vector<Entry> to_stop;
        {
            std::lock_guard<std::mutex> lk(m_);
            to_stop.reserve(entries_.size());
            for (auto& kv : entries_) {
                to_stop.push_back(std::move(kv.second));
            }
            entries_.clear();
        }

        for (auto& entry : to_stop) {
            for (auto& feed : entry.feeds) {
                if (feed) feed->stop();
            }
        }
    }

private:
    struct Entry {
        std::string symbol;
        std::shared_ptr<UIMasterFeed> ui;
        std::vector<std::shared_ptr<IVenueFeed>> feeds;
        std::chrono::steady_clock::time_point last_access{};
        bool pinned{false};
    };

    void build_support_index() {
        for (const auto& pair : canonical_pairs_) {
            std::vector<std::size_t> supported;
            for (std::size_t i = 0; i < venues_.size(); ++i) {
                const auto& venue = venues_[i];
                if (!venue.factory || !venue.api) continue;
                if (venue.api->supports_pair(pair)) {
                    supported.push_back(i);
                }
            }
            if (!supported.empty()) {
                support_index_[pair] = std::move(supported);
                supported_pairs_.push_back(pair);
            }
        }
    }

    bool can_sweep() const {
        return opts_.idle_timeout > std::chrono::seconds::zero() &&
               opts_.sweep_interval > std::chrono::seconds::zero();
    }

    // Background loop that periodically checks for non-hot pairs that have been idle for too long and stops their feeds
    void sweep_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(opts_.sweep_interval);
            if (!running_.load(std::memory_order_relaxed)) break;

            const auto now = std::chrono::steady_clock::now();
            std::vector<Entry> to_stop;

            {
                std::lock_guard<std::mutex> lk(m_);
                for (auto it = entries_.begin(); it != entries_.end();) {
                    if (it->second.pinned) {
                        ++it;
                        continue;
                    }
                    if (now - it->second.last_access <= opts_.idle_timeout) {
                        ++it;
                        continue;
                    }
                    const auto idle_for = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second.last_access).count();
                    std::cout << "[feed] Non-hot pair '" << it->second.symbol
                              << "' no longer requested (idle "
                              << idle_for
                              << "s). Scheduling shutdown."
                              << std::endl;
                    to_stop.push_back(std::move(it->second));
                    it = entries_.erase(it);
                }
            }

            for (auto& entry : to_stop) {
                for (auto& feed : entry.feeds) {
                    if (feed) feed->stop();
                }
                std::cout << "[feed] Pair '" << entry.symbol
                          << "' turned off after inactivity."
                          << std::endl;
            }
        }
    }

    std::vector<VenueRuntime> venues_;
    std::vector<std::string> canonical_pairs_;
    std::unordered_map<std::string, std::vector<std::size_t>> support_index_;
    std::vector<std::string> supported_pairs_;
    std::unordered_set<std::string> hot_pairs_;
    Options opts_;

    mutable std::mutex m_;
    std::unordered_map<std::string, Entry> entries_;
    std::atomic<bool> running_{false};
    std::thread sweeper_;
};
