#pragma once
#include <string>

struct SymbolCodec
{
    // Convert canonical ("BTC-USD") to venue format ("BTC/USD").
    static std::string to_venue(const std::string &venue, const std::string &canonical);
    // Convert venue format ("BTC/USD") to canonical ("BTC-USD").
    static std::string to_canonical(const std::string &venue, const std::string &venue_sym);
};