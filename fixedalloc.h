/**
 * Fixed Allocator
 *
 * This optimizes allocation by using pools of fixed size allocators,
 * and keeping free pointers in a ring buffer.
 * Memory allocation is directly done by mmap() and mlock()d
 * See end of file for list of existing allocators
 */

// how many new blocks to alloc when running out
#define FIXEDALLOC_MORE 128

// Use this define to switch from ring mode to cell mode (less initial overhead, but more overhead when used)
// Also note that a cell allocator will mess up its next allocation in case of buffer overflow which can
// possibly lead to buffer overflow attacks (overwrite position of next allocated mem to match func's return
// addr, and in the next allocated buffer overwrite it with some shell code).
// The default mode uses a ring buffer allocated in a separate mmap (which means that even in case of buffer
// overflow, the write shouldn't reach the next mmap without causing a page fault first)
//#define FIXEDALLOC_CELLMODE

// Lock memory in RAM for better performances (and more memory usage)
#define FIXEDALLOC_MLOCK

#ifdef IN_FIXEDALLOC
#include <stdint.h>

typedef uint32_t fixedalloc_offset_t;

struct fixedalloc_data {
	void *memory;
	uintptr_t allocated; // number of currently allocated blocks
	uintptr_t prealloc,maxalloc; // number of pre/maximum allocated blocks
	uintptr_t block_size; // size of one block
	const char *name; // store name, just in case
#ifndef FIXEDALLOC_CELLMODE
	fixedalloc_offset_t *ring; // ring buffer storing available memory
	fixedalloc_offset_t *ring_end; // end addr of ring
	fixedalloc_offset_t *ring_data_start, *ring_data_end; // start/end pos of data stored in ring
#endif
};

static inline void fixedalloc_init(struct fixedalloc_data *data, uintptr_t size, uintptr_t prealloc, uintptr_t maxalloc, const char*name);
static inline void *fixedalloc_malloc(struct fixedalloc_data *data);
static inline void fixedalloc_free(struct fixedalloc_data *data, void *ptr);

#define ALLOCATOR_DEFINE(_name, _size, _prealloc, _maxalloc) \
static struct fixedalloc_data fixedalloc_ ## _name; \
static void init_ ## _name() __attribute__((constructor)); \
static void init_ ## _name() { fixedalloc_init(&fixedalloc_ ## _name, _size, _prealloc, _maxalloc, #_name); } \
void *malloc_ ## _name() { return fixedalloc_malloc(&fixedalloc_ ## _name); } \
void free_ ## _name(void *ptr) { fixedalloc_free(&fixedalloc_ ## _name, ptr); }
#else
#define ALLOCATOR_DEFINE(_name, _size, _prealloc, _maxalloc) \
void *malloc_ ## _name (); \
void free_ ## _name(void*);
#endif

// for example, this creates functions such as malloc_ptr, free_ptr, malloc_str127, etc
// that will allocate exactly the size of a pointer
//
// If you're unsure of how many items you'll store, switch to cell mode and set it to an arbitrary
// large number (memory / object_size). Using a large value in default mode will use a very large
// amount of memory just for the ring buffer.
ALLOCATOR_DEFINE(ptr,sizeof(void*),512,65536);
ALLOCATOR_DEFINE(str127,128,64,250); // allocate a str127 type that stores 128 bytes (127 bytes string + NUL)

