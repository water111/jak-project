/*!
 * @file kmalloc.cpp
 * GOAL Kernel memory allocator.
 * Simple two-sided bump allocator.
 * DONE
 */

#include <cstring>
#include "common/goal_constants.h"
#include "kmalloc.h"
#include "kprint.h"
#include "kscheme.h"

// global and debug kernel heaps
Ptr<kheapinfo> kglobalheap;
Ptr<kheapinfo> kdebugheap;

void kmalloc_init_globals() {
  // _globalheap and _debugheap
  kglobalheap.offset = GLOBAL_HEAP_INFO_ADDR;
  kdebugheap.offset = DEBUG_HEAP_INFO_ADDR;
}

/*!
 * In the game, this wraps PS2's libc's malloc/calloc.
 * These don't work with GOAL's custom memory management, and this function
 * is unused.
 * DONE, malloc/calloc calls commented out because memory allocated with calloc/malloc
 * cannot trivially be accessed from within GOAL.
 */
Ptr<u8> ksmalloc(Ptr<kheapinfo> heap, s32 size, u32 flags, char const* name) {
  (void)heap;
  (void)size;
  (void)name;
  printf("[ERROR] ksmalloc : cannot be used!\n");
  u32 align = flags & 0xfff;
  Ptr<u8> mem;

  if ((flags & KMALLOC_MEMSET) == 0) {
    // mem = malloc(size + align);
  } else {
    // mem = calloc(1, size + align);
  }

  if (align == KMALLOC_ALIGN_64) {
    mem.offset = (mem.offset + 0x3f) & 0xffffffc0;
  } else if (align == KMALLOC_ALIGN_256) {
    mem.offset = (mem.offset + 0xff) & 0xffffff00;
  }

  return mem;
}

/*!
 * Print the status of a kheap.  This prints to stdout on the runtime,
 * which will not be sent to the Listener.
 * DONE, EXACT
 */
void kheapstatus(Ptr<kheapinfo> heap) {
  Msg(6,
      "[%8x] kheap\n"
      "\tbase: #x%x\n"
      "\ttop-base: #x%x\n"
      "\tcur: #x%x\n"
      "\ttop: #x%x\n",
      heap.offset, heap->base.offset, heap->top_base.offset, heap->current.offset,
      heap->top.offset);
  Msg(6,
      "\t used bot: %d of %d bytes\n"
      "\t used top: %d of %d bytes\n"
      "\t symbols: %d of %d\n",
      heap->current - heap->base, heap->top_base - heap->base, heap->top_base - heap->top,
      heap->top_base - heap->base, NumSymbols, GOAL_MAX_SYMBOLS);

  if (heap == kglobalheap) {
    Msg(6, "\t %d bytes before stack\n", GLOBAL_HEAP_END - heap->current.offset);
  }
}

/*!
 * Initialize a kheapinfo structure, and clear the kheap's memory to 0.
 * DONE, EXACT
 */
Ptr<kheapinfo> kinitheap(Ptr<kheapinfo> heap, Ptr<u8> mem, s32 size) {
  heap->base = mem;
  heap->current = mem;
  heap->top = mem + size;
  heap->top_base = heap->top;
  std::memset(mem.c(), 0, size);
  return heap;
}

/*!
 * Return how much of the bottom (non-temp) allocator is used.
 * DONE, EXACT
 */
u32 kheapused(Ptr<kheapinfo> heap) {
  return heap->current - heap->base;
}

/*!
 * Allocate memory using bump allocation strategy.
 * @param heapPtr : heap to allocate on. If null heap, use global but print a warning
 * @param size    : size of memory needed
 * @param flags   : flags for alignment, top/bottom allocation, set to zero
 * @param name    : name of allocation (printed if things go wrong)
 * @return        : memory.  0 if we run out of room
 * DONE, PRINT ADDED
 */
Ptr<u8> kmalloc(Ptr<kheapinfo> heap, s32 size, u32 flags, char const* name) {
  uint32_t alignment_flag = flags & 0xfff;

  // if we got a null heap, put it on the global heap, but warn about it
  if (!heap.offset) {
    Msg(6, "-----------> kmalloc: alloc %s,  mem %s #x%x (a:%d  %dbytes)\n", "DEBUG", name, -1,
        alignment_flag, size);
    heap = kglobalheap;
  }

  uint32_t memstart;

  if (!(flags & KMALLOC_TOP)) {
    // allocate from bottom
    if (alignment_flag == KMALLOC_ALIGN_64)
      memstart = (0xffffffc0 & (heap->current.offset + 0x40 - 1));
    else if (alignment_flag == KMALLOC_ALIGN_256)
      memstart = (0xffffff00 & (heap->current.offset + 0x100 - 1));
    else  // includes 0x10!
      memstart = (0xfffffff0 & (heap->current.offset + 0x10 - 1));

    if (size == 0) {
      Msg(6, "[WARNING] kmalloc : size 0 allocation from bottom.\n");
      return Ptr<u8>(memstart);
    }

    uint32_t memend = memstart + size;

    if (heap->top.offset < memend) {
      kheapstatus(heap);
      Msg(6, "kmalloc: !alloc mem %s (%d bytes) heap %x\n", name, size, heap.offset);
      return Ptr<u8>(0);
    }

    heap->current.offset = memend;
    if (flags & KMALLOC_MEMSET)
      std::memset(Ptr<u8>(memstart).c(), 0, (size_t)size);
    return Ptr<u8>(memstart);
  } else {
    // allocate from top
    if (alignment_flag == 0) {
      alignment_flag = KMALLOC_ALIGN_16;
    }

    memstart = (heap->top.offset - size) & (-alignment_flag);

    if (size == 0) {
      Msg(6, "[WARNING] kmalloc : size 0 allocation from top\n");
      return Ptr<u8>(memstart);
    }

    if (heap->current.offset >= memstart) {
      Msg(6, "kmalloc: !alloc mem from top %s (%d bytes) heap %x\n", name, size, heap.offset);
      kheapstatus(heap);
      return Ptr<u8>(0);
    }

    heap->top.offset = memstart;

    if (flags & 0x1000)
      std::memset(Ptr<u8>(memstart).c(), 0, (size_t)size);
    return Ptr<u8>(memstart);
  }
}

/*!
 * GOAL does not support automatic freeing of memory. This function does nothing.
 * Programmers wishing to free memory must do it themselves.
 * DONE, PRINT ADDED
 */
void kfree(Ptr<u8> a) {
  (void)a;
  Msg(6, "[ERROR] kmalloc: kfree called\n");
}
