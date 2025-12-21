#pragma once

#include <string>

class IVenueApi {
public:
    virtual ~IVenueApi() = default;

    virtual std::string name() const = 0;
    
    // Checks if the venue supports the given canonical trading pair
    virtual bool supports_pair(const std::string& canonical) const = 0;
};
