//Lock‑Free Fixed‑Size Memory Pool - Simpler version
// LockFreeMemoryPool.hpp
#pragma once
#include <cstddef>   // std::size_t, std::byte
#include <atomic>    // std::atomic
#include <new>       // placement new
#include <cstdlib>   // std::aligned_alloc, std::free

constexpr std::size_t CACHE_LINE = 64;

/**
 * Lock-free fixed-size memory pool.
 *
 * Key properties:
 * - Preallocates N objects of type T.
 * - Lock-free global free list (CAS).
 * - Thread-local cache for ultra-fast allocation.
 * - No dynamic fallback: allocation returns nullptr when pool is exhausted.
 */
template<typename T, std::size_t poolSize>
class LockFreeMemoryPool {
    
    //1. No need for alignas():
    // - FreeNode contains only a pointer, so default alignment (alignof(FreeNode)) is sufficient
    // - Nodes are not concurrently accessed in a way that would cause false sharing
    // - Cache-line alignment would increase memory footprint without benefit here
    //2. Also `next` does not need to be atomic:
    // - It is written by a single thread before the node is published to the free list
    // - After publication, it is only read (never modified) by other threads
    // - Synchronization is provided by atomic operations on `_freeList` (CAS with acquire/release)
    struct FreeNode { FreeNode* next; };

    // Raw contiguous storage for N objects
    std::byte* _buffer;

    // Global lock-free free list head (aligned to avoid false sharing)
    alignas(CACHE_LINE) std::atomic<FreeNode*> _freeList;

    // Per-thread fast-path cache (no atomics needed)
    //static data members belong to the class, not the object (unlike non-static)
    //A thread‑local cache must not belong to a specific pool instance.
    //It must belong to the thread, independent of how many pool objects exist.
    static inline thread_local FreeNode* _localCache;

    // Total bytes required for N objects
    static constexpr std::size_t total_bytes() noexcept {
        return poolSize * sizeof(T);
    }

public:
    LockFreeMemoryPool() {
        // aligned_alloc requires size to be a multiple of alignment
        std::size_t size = total_bytes();

        /*Example:
        size = 240
        CACHE_LINE = 64
        size % CACHE_LINE = 240 % 64 = 48
        size += 64 - 48
        size += 16
        size = 256
        256 % 64 = 0
        So 256 is a valid size for aligned_alloc(64, 256)
        */
        if (size % CACHE_LINE != 0)
            size += CACHE_LINE - (size % CACHE_LINE);

        // Allocate cache-line-aligned storage
        _buffer = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, size));
        if (!_buffer) throw std::bad_alloc{};

        // Build the initial free list (simple singly-linked list)
        FreeNode* head = nullptr;
        for (std::size_t i = 0; i < poolSize; ++i) {
            auto* new_node = reinterpret_cast<FreeNode*>(_buffer + i * sizeof(T));
            new_node->next = head;
            head = new_node;
        }

        _freeList.store(head, std::memory_order_release);
    }

    ~LockFreeMemoryPool() {
        //Cant use delte[] as malloc/calloc/aligned_alloc needs free()
        std::free(_buffer);
    }

    LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool& operator=(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool(LockFreeMemoryPool&&) = delete;
    LockFreeMemoryPool& operator=(LockFreeMemoryPool&&) = delete;

    /**
     * Allocate raw storage for one T.
     * Returns nullptr if the pool is exhausted.
     */
    T* allocate() noexcept {
        // Fast path: thread-local cache (no atomics)
        if (_localCache) {
            FreeNode* n = _localCache;
            _localCache = n->next;
            return reinterpret_cast<T*>(n);
        }

        // Slow path: global lock-free free list
        FreeNode* head = _freeList.load(std::memory_order_acquire);
        while (head) {
            FreeNode* next = head->next;

            // Attempt to pop the head using CAS
            if (_freeList.compare_exchange_weak(
                    head, next,
                    std::memory_order_acq_rel,//not memory_order_release because I shud also get what's released by deallocate()(in real scenario)
                    std::memory_order_acquire)) {

                return reinterpret_cast<T*>(head);
            }
        }

        // Pool exhausted
        return nullptr;
    }

    /**
     * Return storage back to the pool.
     * Returned objects go into the thread-local cache first.
     */
    void deallocate(T* ptr) noexcept {
        auto* new_node = reinterpret_cast<FreeNode*>(ptr);

        // Push into thread-local cache (fast path)
        new_node->next = _localCache;
        _localCache = new_node;
    }
};

// Definition of thread-local cache pointer
/*No need as _localCache is inline 
template<typename T, std::size_t poolSize>
thread_local typename LockFreeMemoryPool<T, poolSize>::FreeNode*
    LockFreeMemoryPool<T, poolSize>::_localCache {nullptr};
*/
