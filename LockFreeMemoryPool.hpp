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
    // Global lock-free free list head (aligned to avoid false sharing)
    alignas(CACHE_LINE) std::atomic<FreeNode*> _freeList;

    // Raw contiguous storage for N objects
    std::byte* _buffer; //Not T*

    // Per-thread fast-path cache (no atomics needed)
    //static data members belong to the class, not the object (unlike non-static)
    //A thread‑local cache must not belong to a specific pool instance.
    //It must belong to the thread, independent of how many pool objects exist.
    static inline thread_local FreeNode* _localCache = nullptr;
    static inline thread_local std::size_t _localCacheCount = 0;

    static constexpr std::size_t LOCAL_CACHE_LIMIT = 32;
    static constexpr std::size_t FLUSH_BATCH_SIZE  = 16; 

    // Total bytes required for N objects
    static constexpr std::size_t total_bytes() noexcept {
        return poolSize * sizeof(T);
    }

    //round up sizeof(T) to next multiple of alignof(T)
    //i.e. smallest number ≥ sizeof(T) that is a multiple of alignof(T)
    /* Example:
       sizeof(T) = 24
       alignof(T) = 16
       Step 1: 24 + 16 - 1 = 39
       Step 2:
         alignof(T) = 16  -> 10000
         alignof(T)-1     -> 01111
         ~(alignof(T)-1)  -> 11110000
       Step 3:
         39 in binary = 100111
         mask         = 11110000
         result       = 100000 (32)
     */
    static constexpr std::size_t roundUpToNextAlignofT(){
        return (sizeof(T) + alignof(T) - 1) & 
                ~(alignof(T) - 1);
    }

public:
    
    LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool& operator=(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool(LockFreeMemoryPool&&) = delete;
    LockFreeMemoryPool& operator=(LockFreeMemoryPool&&) = delete;

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
        constexpr std::size_t stride = roundUpToNextAlignofT();
        for (std::size_t i = 0; i < poolSize; ++i) {
            /*T may require alignment greater than 1:
             * alignof(T) could be 8, 16, 32, etc.
             * _buffer + i * sizeof(T) does NOT guarantee alignment
            */
            //auto* new_node = reinterpret_cast<FreeNode*>(_buffer + i * sizeof(T));
            auto* new_node = reinterpret_cast<FreeNode*>(_buffer + i * stride);
            new_node->next = head;
            head = new_node;
        }

        //No need for release as :
        //* no other thread can legally observe _freeList yet
        //* the object is not “published” to other threads during construction
        _freeList.store(head, std::memory_order_relaxed);
    }

    ~LockFreeMemoryPool() {
        //Cant use delte[] as malloc/calloc/aligned_alloc needs free()
        std::free(_buffer);
    }

    /**
     * Allocate raw storage for one T.
     * Returns nullptr if the pool is exhausted.
     */
    T* allocate() noexcept {
        // Fast path: thread-local cache (no atomics)
        if (_localCache) {
            FreeNode* n = _localCache;
            _localCache = n->next;
            _localCacheCount-- ;
            return reinterpret_cast<T*>(n);
        }

        // Slow path: global lock-free free list
        while (true) 
        {
           FreeNode* old_head = _freeList.load(std::memory_order_acquire);
           if (!old_head)
               return nullptr;

           FreeNode* new_head = old_head->next;

           if (_freeList.compare_exchange_weak(
                  old_head,
                  new_head,
                  std::memory_order_acquire,
                  std::memory_order_relaxed)) 
            {
              return reinterpret_cast<T*>(old_head);
            }
         }
        // Pool exhausted
        return nullptr;
    }

    /**
     * Return storage back to the pool.
     * Returned objects go into the thread-local cache first.
     */
    void deallocate(T* ptr) noexcept 
    {
        auto* node = reinterpret_cast<FreeNode*>(ptr);
        // Push into thread-local cache (fast path)
        node->next = _localCache;
        _localCache = node;

        // Flush local cache so that we avoid a particular thread caches all nodes and other threads starve
        if (++_localCacheCount >= LOCAL_CACHE_LIMIT) {
            flush_local_cache(); 
        }
    }

private: 
    void flush_local_cache() noexcept 
    {

       if (!_localCache)
           return;

       // Find batch tail
       FreeNode* tail = _localCache;
       std::size_t count = 1;

       while (count < FLUSH_BATCH_SIZE && tail->next) {
           tail = tail->next;
           ++count;
       }

       // If only one node, skip flush
       if (count == 1)
           return;

       FreeNode* batch_head = _localCache;
       FreeNode* remaining  = tail->next;

       _localCache = remaining;
       _localCacheCount -= count;

    // Push batch to global list
    while (true) 
    {
        FreeNode* old_head = _freeList.load(std::memory_order_relaxed);
        tail->next = old_head;

        if (_freeList.compare_exchange_weak(
                old_head,
                batch_head,
                std::memory_order_release,
                std::memory_order_relaxed)) 
        {
            break;
        }
    }//while()
  }
};

// Definition of thread-local cache pointer
/*No need as _localCache is inline 
template<typename T, std::size_t poolSize>
thread_local typename LockFreeMemoryPool<T, poolSize>::FreeNode*
    LockFreeMemoryPool<T, poolSize>::_localCache {nullptr};
*/
