
================================================================================
README.md — Lock‑Free Fixed‑Size Memory Pool - Simpler version
================================================================================

## 1. Overview

This memory pool provides a compact, high‑performance mechanism for allocating
and deallocating fixed‑size objects. The design focuses on:

- predictable allocation latency
- lock‑free concurrency
- cache‑line‑aware layout
- minimal API surface

The pool preallocates storage for N objects of type T and manages them through a
lock‑free free list combined with a thread‑local fast path.

--------------------------------------------------------------------------------
## 2. Core Concepts

### 2.1 Preallocated Storage
A contiguous block of memory is allocated once during construction. This avoids
runtime heap allocation and ensures predictable performance.

### 2.2 Free List
Each free slot is represented by a small node stored directly inside the pool’s
memory. The free list forms a simple singly‑linked list:

    FreeNode -> FreeNode -> FreeNode -> ...

### 2.3 Lock‑Free CAS Operations
The global free list is updated using atomic compare‑exchange operations. This
allows multiple threads to allocate concurrently without locks.

### 2.4 Thread‑Local Cache
Each thread maintains a local cache of free nodes. Allocations from this cache
require no atomic operations and provide extremely low latency.

### 2.5 Cache‑Line Alignment
The pool aligns the underlying storage to a 64‑byte boundary. This reduces false
sharing and improves cache locality.

--------------------------------------------------------------------------------
## 3. Allocation Flow

1. Check the thread‑local cache.
   - If a node is available, return it immediately.

2. Otherwise, pop a node from the global free list using CAS.
   - If successful, return the node.
   - If the list is empty, return nullptr.

This ensures constant‑time allocation in the common case.

--------------------------------------------------------------------------------
## 4. Deallocation Flow

1. Convert the pointer back into a FreeNode.
2. Push it into the thread‑local cache.

No atomic operations are required for deallocation.

--------------------------------------------------------------------------------
## 5. Usage Pattern

The pool returns raw storage for one object of type T. Construction and
destruction are performed manually:

    T* p = pool.allocate();
    new (p) T(...);     // placement new
    p->~T();            // manual destructor call
    pool.deallocate(p);

This mirrors the behavior of standard C++ allocators.

--------------------------------------------------------------------------------
## 6. Exhaustion Behavior

If all N objects are in use, allocate() returns nullptr. This avoids hidden
dynamic allocation and keeps behavior predictable.

--------------------------------------------------------------------------------
## 7. Strengths of This Design

- extremely low allocation latency
- no locks
- no fragmentation
- predictable memory footprint
- realistic enough for performance‑critical systems
- cache‑friendly layout

--------------------------------------------------------------------------------
## 8. Intentional Limitations

- fixed capacity (no resizing)
- no automatic object construction or destruction
- no variable‑sized allocations
- no ABA protection

These limitations keep the implementation compact and easy to explain.

--------------------------------------------------------------------------------
## 9. Summary

This memory pool demonstrates:

- lock‑free programming
- thread‑local fast paths
- cache‑line alignment
- manual memory management
- allocator‑style design

The implementation is concise, technically correct.
scenarios where clarity and reasoning matter more than production‑grade
complexity.

================================================================================
END OF README.md
================================================================================


