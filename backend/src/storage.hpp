#pragma once
#include "order.hpp"
#include <vector>
#include <optional>
#include <string>

class IOrderStore
{
public:
    virtual ~IOrderStore() = default;

    // Adds order, assigns id+timestamp, returns assigned id
    virtual std::string add(Order o) = 0;

    // Returns copy of all orders (cheap for small demo; can stream later)
    virtual std::vector<Order> list() const = 0;

    // Lookup by id
    virtual std::optional<Order> get(const std::string &id) const = 0;

    // Cancel by id (if still NEW or PARTIALLY_FILLED). Returns true if changed.
    virtual bool cancel(const std::string &id) = 0;
};

IOrderStore *make_memory_store();