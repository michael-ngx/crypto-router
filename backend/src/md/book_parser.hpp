#pragma once
#include "book_events.hpp"
#include <memory>
#include <string>
#include <vector>

// Uniform interface for any venue book parser (snapshot + incremental updates).
struct IBookParser {
    virtual ~IBookParser() = default;

    // Parse a raw JSON text frame into one or more BookEvent(s).
    // Return true if the message is successfully parsed as a relevant book event.
    virtual bool parse(const std::string& raw, std::vector<BookEvent>& out) = 0;
};
