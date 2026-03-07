#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "book_events.hpp"
#include "book_snapshot.hpp"

// Per-venue full-depth limit order book.
// - BookEventSnapshot replaces both sides with absolute sizes (Upsert only).
// - BookEventDelta is absolute size at price (0 or Delete => erase).
// - Single writer model: one consumer thread mutates this book.
// - Readers consume immutable snapshots published by VenueFeed.
class Book {
public:
    using BidMap = std::map<double, double, std::greater<double>>; // best-first
    using AskMap = std::map<double, double, std::less<double>>;    // best-first

    Book(std::string venue, std::string symbol)
        : venue_(std::move(venue)), symbol_(std::move(symbol)) {}

    // Single-event apply.
    void apply(const BookEventSnapshot& snap) {
        if (!matches(snap.venue, snap.symbol)) return;
        apply_unlocked(snap);
    }
    void apply(const BookEventDelta& d) {
        if (!matches(d.venue, d.symbol)) return;
        apply_unlocked(d);
    }
    void apply(const BookEvent& ev) {
        std::visit([this](auto&& e){ apply_unlocked(e); }, ev);
    }

    // Batch apply.
    void apply_many(const std::vector<BookEvent>& evs) {
        for (const auto& ev : evs) {
            std::visit([this](auto&& e){ apply_unlocked(e); }, ev);
        }
    }

    // Read API for publisher path.
    std::vector<std::pair<double,double>> top_bids(std::size_t n) const {
        ensure_bid_curve();
        return take_top_pairs(bid_curve_, n);
    }
    std::vector<std::pair<double,double>> top_asks(std::size_t n) const {
        ensure_ask_curve();
        return take_top_pairs(ask_curve_, n);
    }
    // O(1) top-of-book access from canonical maps (used by publish gating).
    std::optional<std::pair<double,double>> best_bid() const noexcept {
        if (bids_.empty()) return std::nullopt;
        const auto& it = *bids_.begin();
        return std::make_pair(it.first, it.second);
    }
    std::optional<std::pair<double,double>> best_ask() const noexcept {
        if (asks_.empty()) return std::nullopt;
        const auto& it = *asks_.begin();
        return std::make_pair(it.first, it.second);
    }

    std::size_t bid_levels() const noexcept { return bids_.size(); }
    std::size_t ask_levels() const noexcept { return asks_.size(); }

    // Copy precomputed full-depth levels for immutable snapshot publication.
    void copy_snapshot_levels(std::vector<BookSnapshotLevel>& bids_out,
                              std::vector<BookSnapshotLevel>& asks_out) const {
        ensure_bid_curve();
        ensure_ask_curve();
        bids_out = bid_curve_;
        asks_out = ask_curve_;
    }

    const std::string& venue()  const noexcept { return venue_; }
    const std::string& symbol() const noexcept { return symbol_; }

    void clear() {
        bids_.clear();
        asks_.clear();
        bid_curve_.clear();
        ask_curve_.clear();
        bid_curve_dirty_ = false;
        ask_curve_dirty_ = false;
        last_seq_ = 0;
    }

private:
    static bool valid_price(double price) noexcept {
        return std::isfinite(price) && price > 0.0;
    }

    static bool valid_size(double size) noexcept {
        return std::isfinite(size) && size > 0.0;
    }

    // -------- apply helpers --------
    void apply_unlocked(const BookEventSnapshot& snap) {
        bids_.clear();
        asks_.clear();
        std::uint64_t max_seq_in_snap = 0;
        for (const auto& lvl : snap.levels) {
            if (lvl.op == BookOp::Delete) continue;
            if (!valid_price(lvl.price) || !valid_size(lvl.size)) continue;
            if (lvl.side == BookSide::Bid) bids_[lvl.price] = lvl.size;
            else                           asks_[lvl.price] = lvl.size;
            if (lvl.seq > max_seq_in_snap) max_seq_in_snap = lvl.seq;
        }
        if (max_seq_in_snap) last_seq_ = max_seq_in_snap;
        bid_curve_dirty_ = true;
        ask_curve_dirty_ = true;
    }

    void apply_unlocked(const BookEventDelta& d) {
        if (d.seq && last_seq_ && d.seq <= last_seq_) return;
        if (!valid_price(d.price)) return;

        if (d.side == BookSide::Bid) {
            apply_one(bids_, d);
            bid_curve_dirty_ = true;
        } else {
            apply_one(asks_, d);
            ask_curve_dirty_ = true;
        }
        if (d.seq) last_seq_ = d.seq;
    }

    static void apply_one(BidMap& side, const BookEventDelta& d) {
        if (d.op == BookOp::Delete || !valid_size(d.size)) {
            auto it = side.find(d.price);
            if (it != side.end()) side.erase(it);
        } else {
            side[d.price] = d.size;
        }
    }
    static void apply_one(AskMap& side, const BookEventDelta& d) {
        if (d.op == BookOp::Delete || !valid_size(d.size)) {
            auto it = side.find(d.price);
            if (it != side.end()) side.erase(it);
        } else {
            side[d.price] = d.size;
        }
    }

    void ensure_bid_curve() const {
        if (!bid_curve_dirty_) return;
        rebuild_curve(bids_, bid_curve_);
        bid_curve_dirty_ = false;
    }

    void ensure_ask_curve() const {
        if (!ask_curve_dirty_) return;
        rebuild_curve(asks_, ask_curve_);
        ask_curve_dirty_ = false;
    }

    template <class OrderedMap>
    static void rebuild_curve(const OrderedMap& side, std::vector<BookSnapshotLevel>& out) {
        out.clear();
        out.reserve(side.size());
        double cum_qty = 0.0;
        double cum_notional = 0.0;
        for (const auto& [px, sz] : side) {
            cum_qty += sz;
            cum_notional += (px * sz);
            out.push_back(BookSnapshotLevel{px, sz, cum_qty, cum_notional});
        }
    }

    static std::vector<std::pair<double,double>> take_top_pairs(
        const std::vector<BookSnapshotLevel>& levels,
        std::size_t n) {
        std::vector<std::pair<double,double>> out;
        out.reserve(std::min(n, levels.size()));
        std::size_t taken = 0;
        for (const auto& lvl : levels) {
            if (taken++ >= n) break;
            out.emplace_back(lvl.price, lvl.size);
        }
        return out;
    }

    bool matches(const std::string& v, const std::string& s) const noexcept {
        return v == venue_ && s == symbol_;
    }

    // -------- state --------
    std::string venue_;
    std::string symbol_;

    BidMap bids_;
    AskMap asks_;
    std::uint64_t last_seq_{0}; // 0 => unknown; otherwise last applied seq

    // Cached cumulative curves for publisher snapshots.
    mutable std::vector<BookSnapshotLevel> bid_curve_;
    mutable std::vector<BookSnapshotLevel> ask_curve_;
    mutable bool bid_curve_dirty_{true};
    mutable bool ask_curve_dirty_{true};
};
