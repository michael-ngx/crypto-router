#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "venue_factory.hpp"
#include "coinbase/factory.hpp"
#include "kraken/factory.hpp"

class VenueRegistry {
public:
    static const VenueRegistry& instance() {
        static VenueRegistry registry;
        return registry;
    }

    const VenueFactory* find(std::string_view name) const {
        auto it = factories_.find(std::string(name));
        if (it == factories_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> list_names() const {
        std::vector<std::string> names;
        names.reserve(factories_.size());
        for (const auto& kv : factories_) {
            names.push_back(kv.first);
        }
        return names;
    }

private:
    VenueRegistry() {
        register_factory(make_coinbase_factory());
        register_factory(make_kraken_factory());
    }

    void register_factory(VenueFactory factory) {
        if (factory.name.empty() ||
            !factory.make_feed ||
            !factory.make_api ||
            !factory.to_venue_symbol) {
            return;
        }
        factories_.emplace(factory.name, std::move(factory));
    }

    std::unordered_map<std::string, VenueFactory> factories_;
};
