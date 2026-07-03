// LockFreeMemoryPool.cpp
#include "LockFreeMemoryPool.hpp"
#include <iostream>  
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>


//Test-1
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

//Test-2 & 3:
struct MyObj {
    int x;
    int y;
};

constexpr std::size_t POOL_SIZE = 1024;
LockFreeMemoryPool<MyObj, POOL_SIZE> pool;

void single_thread_test() {

    std::vector<MyObj*> ptrs;

    // Allocate all objects
    for (int i = 0; i < POOL_SIZE; ++i) {
        auto* p = pool.allocate();
        assert(p != nullptr);
        p->x = i;
        p->y = i * 2;
        ptrs.push_back(p);
    }

    // Pool should be exhausted
    assert(pool.allocate() == nullptr);

    // Free all
    for (auto* p : ptrs) {
        pool.deallocate(p);
    }

    ptrs.clear();

    // Should reuse again
    for (int i = 0; i < POOL_SIZE; ++i) {
        auto* p = pool.allocate();
        assert(p != nullptr);
        p->x = i;
    }

    std::cout << "[single_thread_test] OK\n";
}

void worker(int id, int iterations) {

    std::vector<MyObj*> local;

    for (int i = 0; i < iterations; ++i) {

        auto* p = pool.allocate();

        if (p) {
            p->x = id;
            p->y = i;
            local.push_back(p);
        }

        // randomly free
        if (!local.empty() && (i % 3 == 0)) {
            pool.deallocate(local.back());
            local.pop_back();
        }
    }

    for (auto* p : local) {
        pool.deallocate(p);
    }
}

void multithread_test() {

    constexpr int THREADS = 4;
    constexpr int ITER = 50'000;

    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back(worker, t, ITER);
    }

    for (auto& th : threads) {
        th.join();
    }

    std::cout << "[multithread_test] OK\n";
}



int main() {
    //Test-1
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

    //Test-2
    single_thread_test();
    //Test-3
    multithread_test();

    std::cout << "All tests passed\n";

    return 0;
}



