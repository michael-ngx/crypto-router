#pragma once
#include <map>
#include <vector>
#include <string>
#include <shared_mutex>
#include <optional>
#include <cstddef>
#include <variant>
#include <algorithm>
#include <utility>
#include <cstdint>

#include "book_events.hpp"

// Per-venue full-depth limit order book.
// - BookSnapshot replaces both sides with absolute sizes (Upsert only).
// - BookDelta is absolute size at price (0 or Delete => erase).
// - Readers request top-N on read; book keeps all visible levels.
// - Uses shared_mutex for concurrent reads.
class Book {
public:
    Book(std::string venue, std::string symbol)
        : venue_(std::move(venue)), symbol_(std::move(symbol)) {}

    // Single-event apply (locks internally)
    void apply(const BookSnapshot& snap) {
        if (!matches(snap.venue, snap.symbol)) return;
        std::unique_lock lk(m_);
        apply_unlocked(snap);
    }
    void apply(const BookDelta& d) {
        if (!matches(d.venue, d.symbol)) return;
        std::unique_lock lk(m_);
        apply_unlocked(d);
    }
    void apply(const BookEvent& ev) {
        std::unique_lock lk(m_);
        std::visit([this](auto&& e){ apply_unlocked(e); }, ev);
    }

    // Batch apply: one lock for the whole batch
    void apply_many(const std::vector<BookEvent>& evs) {
        std::unique_lock lk(m_);
        for (const auto& ev : evs) {
            std::visit([this](auto&& e){ apply_unlocked(e); }, ev);
        }
    }

    // Read API
    std::vector<std::pair<double,double>> top_bids(std::size_t n) const {
        std::shared_lock lk(m_);
        return take_first_n(bids_, n);
    }
    std::vector<std::pair<double,double>> top_asks(std::size_t n) const {
        std::shared_lock lk(m_);
        return take_first_n(asks_, n);
    }
    std::optional<std::pair<double,double>> best_bid() const {
        std::shared_lock lk(m_);
        if (bids_.empty()) return std::nullopt;
        const auto& it = *bids_.begin();
        return std::make_pair(it.first, it.second);
    }
    std::optional<std::pair<double,double>> best_ask() const {
        std::shared_lock lk(m_);
        if (asks_.empty()) return std::nullopt;
        const auto& it = *asks_.begin();
        return std::make_pair(it.first, it.second);
    }

    std::size_t bid_levels() const { std::shared_lock lk(m_); return bids_.size(); }
    std::size_t ask_levels() const { std::shared_lock lk(m_); return asks_.size(); }

    const std::string& venue()  const noexcept { return venue_; }
    const std::string& symbol() const noexcept { return symbol_; }

    void clear() {
        std::unique_lock lk(m_);
        bids_.clear(); asks_.clear();
        last_seq_ = 0;
    }

private:
    using BidMap = std::map<double, double, std::greater<double>>; // best-first
    using AskMap = std::map<double, double, std::less<double>>;    // best-first

    // -------- unlocked helpers (caller holds m_) --------
    void apply_unlocked(const BookSnapshot& snap) {
        // Replace both sides with absolute sizes from snapshot
        bids_.clear();
        asks_.clear();
        std::uint64_t max_seq_in_snap = 0;
        for (const auto& lvl : snap.levels) {
            if (lvl.op == BookOp::Delete || lvl.size == 0.0) continue;
            if (lvl.side == BookSide::Bid) bids_[lvl.price] = lvl.size;
            else                           asks_[lvl.price] = lvl.size;
            if (lvl.seq > max_seq_in_snap) max_seq_in_snap = lvl.seq;
        }
        if (max_seq_in_snap) last_seq_ = max_seq_in_snap; // advance sequence watermark if provided
    }

    void apply_unlocked(const BookDelta& d) {
        // If venue provides monotonic seq, drop stale deltas.
        if (d.seq && last_seq_ && d.seq <= last_seq_) return;

        if (d.side == BookSide::Bid) {
            apply_one(bids_, d);
        } else {
            apply_one(asks_, d);
        }
        if (d.seq) last_seq_ = d.seq;
    }

    static void apply_one(BidMap& side, const BookDelta& d) {
        if (d.op == BookOp::Delete || d.size == 0.0) {
            auto it = side.find(d.price);
            if (it != side.end()) side.erase(it);
        } else {
            side[d.price] = d.size;
        }
    }
    static void apply_one(AskMap& side, const BookDelta& d) {
        if (d.op == BookOp::Delete || d.size == 0.0) {
            auto it = side.find(d.price);
            if (it != side.end()) side.erase(it);
        } else {
            side[d.price] = d.size;
        }
    }

    template <class OrderedMap>
    static std::vector<std::pair<double,double>> take_first_n(const OrderedMap& m, std::size_t n) {
        std::vector<std::pair<double,double>> out;
        out.reserve(std::min(n, m.size()));
        std::size_t taken = 0;
        for (const auto& [px, sz] : m) {
            if (taken++ >= n) break;
            out.emplace_back(px, sz);
        }
        return out;
    }

    bool matches(const std::string& v, const std::string& s) const noexcept {
        return v == venue_ && s == symbol_;
    }

    // -------- state --------
    std::string venue_;
    std::string symbol_;

    mutable std::shared_mutex m_;
    BidMap bids_;
    AskMap asks_;
    std::uint64_t last_seq_{0}; // 0 => unknown; otherwise last applied seq
};