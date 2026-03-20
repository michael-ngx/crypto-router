#pragma once

#include <cstdint>

namespace md::liveness {

// Transport is considered stale when no frame has been observed for this long.
inline constexpr std::int64_t kTransportStaleNs = 10'000'000'000; // 10 seconds
// Feed is considered quiet when no parsed book update was observed within this window.
inline constexpr std::int64_t kQuietBookNs = 5'000'000'000; // 5 seconds

} // namespace md::liveness
