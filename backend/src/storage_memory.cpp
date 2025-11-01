#include "storage.hpp"
#include <mutex>
#include <atomic>
#include <sstream>
#include <algorithm>

namespace
{
    std::atomic<uint64_t> g_seq{1};
    static std::string next_id()
    {
        uint64_t v = g_seq.fetch_add(1, std::memory_order_relaxed);
        std::ostringstream os;
        os << "ord-" << v;
        return os.str();
    }
    static std::int64_t now_ns()
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
}

class MemoryStore final : public IOrderStore
{
    mutable std::mutex mtx_;
    std::vector<Order> orders_;

public:
    std::string add(Order o) override
    {
        std::scoped_lock lk(mtx_);
        o.id = next_id();
        o.ts_ns = now_ns();
        orders_.push_back(o);
        return o.id;
    }

    std::vector<Order> list() const override
    {
        std::scoped_lock lk(mtx_);
        return orders_;
    }

    std::optional<Order> get(const std::string &id) const override
    {
        std::scoped_lock lk(mtx_);
        auto it = std::find_if(orders_.begin(), orders_.end(), [&](const Order &o)
                               { return o.id == id; });
        if (it == orders_.end())
            return std::nullopt;
        return *it;
    }

    bool cancel(const std::string &id) override
    {
        std::scoped_lock lk(mtx_);
        for (auto &o : orders_)
        {
            if (o.id == id)
            {
                if (o.status == OrderStatus::NEW || o.status == OrderStatus::PARTIALLY_FILLED)
                {
                    o.status = OrderStatus::CANCELED;
                    return true;
                }
                return false; // already terminal
            }
        }
        return false; // not found
    }
};

IOrderStore *make_memory_store() { return new MemoryStore(); }