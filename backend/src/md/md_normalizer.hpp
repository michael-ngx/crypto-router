#pragma once
#include "md_types.hpp"
#include <string>

struct IMarketNormalizer
{
    virtual ~IMarketNormalizer() = default;
    // Return true if a ticker was parsed into 'out'
    virtual bool parse_ticker(const std::string &raw, NormalizedTick &out) = 0;
};