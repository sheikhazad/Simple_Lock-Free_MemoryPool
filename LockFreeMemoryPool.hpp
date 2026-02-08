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
    // Node stored inside the free list
    struct FreeNode { FreeNode* next; };

    // Raw contiguous storage for N objects
    std::byte* buffer;

    // Global lock-free free list head (aligned to avoid false sharing)
    alignas(CACHE_LINE) std::atomic<FreeNode*> freeList;

    // Per-thread fast-path cache (no atomics needed)
    //static data members belong to the class, not the object (unlike non-static)
    //A thread‑local cache must not belong to a specific pool instance.
    //It must belong to the thread, independent of how many pool objects exist.
    static thread_local FreeNode* _localCache;

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
        buffer = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, size));
        if (!buffer) throw std::bad_alloc{};

        // Build the initial free list (simple singly-linked list)
        FreeNode* head = nullptr;
        for (std::size_t i = 0; i < poolSize; ++i) {
            auto* node = reinterpret_cast<FreeNode*>(buffer + i * sizeof(T));
            node->next = head;
            head = node;
        }

        freeList.store(head, std::memory_order_release);
    }

    ~LockFreeMemoryPool() {
        //Cant use delte[] as malloc/calloc/aligned_alloc needs free()
        std::free(buffer);
    }

    LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool& operator=(const LockFreeMemoryPool&) = delete;

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
        FreeNode* head = freeList.load(std::memory_order_acquire);
        while (head) {
            FreeNode* next = head->next;

            // Attempt to pop the head using CAS
            if (freeList.compare_exchange_weak(
                    head, next,
                    std::memory_order_acq_rel,
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
        auto* node = reinterpret_cast<FreeNode*>(ptr);

        // Push into thread-local cache (fast path)
        node->next = _localCache;
        _localCache = node;
    }
};

// Definition of thread-local cache pointer
template<typename T, std::size_t poolSize>
thread_local typename LockFreeMemoryPool<T, poolSize>::FreeNode*
    LockFreeMemoryPool<T, poolSize>::_localCache {nullptr};
