#pragma once

#include <functional>
#include <memory>
#include <string>

struct IVenueFeed;
class IVenueApi;

struct VenueFactory {
    std::string name;
    std::function<std::shared_ptr<IVenueFeed>(const std::string& canonical)> make_feed;
    std::function<std::unique_ptr<IVenueApi>()> make_api;
    std::function<std::string(const std::string& canonical)> to_venue_symbol;
};
