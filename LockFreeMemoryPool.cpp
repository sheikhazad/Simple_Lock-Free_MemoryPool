// LockFreeMemoryPool.cpp
#include "LockFreeMemoryPool.hpp"
#include <iostream>  

// ===================== Example Usage ===================== //

struct alignas(CACHE_LINE) Order {
    std::uint64_t id;
    double price;
    int qty;

    Order(std::uint64_t id_, double price_, int qty_)
        : id(id_), price(price_), qty(qty_) {}

    void print() const {
        std::cout << "Order ID=" << id
                  << " Price=" << price
                  << " Qty=" << qty << "\n";
    }
};

int main() {
    LockFreeMemoryPool<Order, 4> pool; // Small pool for demonstration

    // Allocate a few objects
    Order* o1 = pool.allocate();
    Order* o2 = pool.allocate();
    Order* o3 = pool.allocate();
    Order* o4 = pool.allocate();

    //Placement new
    // Construct objects in-place
    new (o1) Order(1, 100.5, 10);
    new (o2) Order(2, 101.0, 20);
    new (o3) Order(3, 102.5, 30);
    new (o4) Order(4, 103.0, 40);

    o1->print();
    o2->print();
    o3->print();
    o4->print();

    // Pool is now exhausted
    Order* o5 = pool.allocate();
    if (!o5) {
        std::cout << "Pool exhausted\n";
    }

    // Destroy and deallocate
    //Need to call o1->~Order() to destroy manually as o1 was created manually, otherwise internal resource leak. 
    //pool.deallocate(o1) does not call Order's destructor
    o1->~Order(); pool.deallocate(o1);
    o2->~Order(); pool.deallocate(o2);

    // Now allocation succeeds again
    Order* o6 = pool.allocate();
    new (o6) Order(6, 110.0, 60);
    o6->print();

    o3->~Order(); pool.deallocate(o3);
    o4->~Order(); pool.deallocate(o4);
    o6->~Order(); pool.deallocate(o6);

    return 0;
}
