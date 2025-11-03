#pragma once
#include <string>
#include <cstdint>
#include <variant>
#include <vector>

enum class BookSide : uint8_t
{
    Bid = 0,
    Ask = 1
};
enum class BookOp : uint8_t
{
    Upsert = 0,
    Delete = 1
};

struct BookDelta
{
    std::string venue;  // "coinbase", "kraken"
    std::string symbol; // canonical "BTC-USD"
    BookSide side;
    double price{0};
    double size{0}; // size==0 implies delete for some venues
    BookOp op{BookOp::Upsert};
    std::uint64_t seq{0}; // venue sequence if available (0 if not)
    std::int64_t ts_ns{0};
};

struct BookSnapshot
{
    std::string venue;  // venue id
    std::string symbol; // canonical
    // We encode snapshot as a vector of deltas (Upsert) you can apply in order.
    std::vector<BookDelta> levels;
    std::int64_t ts_ns{0};
};

using BookEvent = std::variant<BookSnapshot, BookDelta>;