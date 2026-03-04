#pragma once

#include <string>
#include <vector>

class IVenueApi {
public:
    virtual ~IVenueApi() = default;

    virtual std::string name() const = 0;
    
    // Returns all supported canonical pairs for this venue (e.g. "BTC-USD").
    virtual std::vector<std::string> list_supported_pairs() const = 0;
};
