#include "storage.hpp"
#include <iostream>
#include <cassert>

static void print(const Order& o){
    std::cout << o.id << " | " << o.symbol << " | " << to_cstr(o.side)
              << " | " << to_cstr(o.type) << " | px=" << o.price
              << " | qty=" << o.qty << " | status=" << to_cstr(o.status)
              << " | ts_ns=" << o.ts_ns << "\n";
}

int main(){
    std::unique_ptr<IOrderStore> store(make_memory_store());

    Order a; a.symbol = "BTC-USD"; a.side = Side::BUY; a.type = OrderType::LIMIT; a.price = 60000.0; a.qty = 0.01;
    Order b; b.symbol = "ETH-USD"; b.side = Side::SELL; b.type = OrderType::LIMIT; b.price = 2500.0;  b.qty = 0.5;

    auto id1 = store->add(a);
    auto id2 = store->add(b);

    auto all = store->list();
    std::cout << "\n== After add (" << all.size() << ") ==\n";
    for (auto& o : all) print(o);

    auto got1 = store->get(id1);
    assert(got1.has_value());

    bool canceled = store->cancel(id1);
    assert(canceled);

    auto post = store->list();
    std::cout << "\n== After cancel " << id1 << " ==\n";
    for (auto& o : post) print(o);

    // Sanity checks
    auto check1 = store->get(id1); assert(check1 && check1->status == OrderStatus::CANCELED);
    auto check2 = store->get(id2); assert(check2 && check2->status == OrderStatus::NEW);

    std::cout << "\nOK\n";
    return 0;
}
