/**
 * Fixed Allocator
 *
 * This optimizes allocation by using pools of fixed size allocators,
 * and keeping free pointers in a ring buffer.
 * Memory allocation is directly done by mmap() and mlock()d
 * See end of file for list of existing allocators
 */

#ifdef IN_FIXEDALLOC
#include <stdint.h>

typedef uint32_t fixedalloc_offset_t;

struct fixedalloc_data;
static inline void fixedalloc_init(struct fixedalloc_data *data, size_t size, fixedalloc_offset_t prealloc, fixedalloc_offset_t maxalloc, const char*name);
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

#include "fixedalloc_cfg.h"

// the struct needs to be included *after* the cfg

#ifdef IN_FIXEDALLOC
struct fixedalloc_data {
	void *memory;
	fixedalloc_offset_t allocated; // number of currently allocated blocks
	fixedalloc_offset_t prealloc,maxalloc; // number of pre/maximum allocated blocks
	size_t block_size; // size of one block
	const char *name; // store name, just in case
#ifdef FIXEDALLOC_CELLMODE
	fixedalloc_offset_t next;
#else
	fixedalloc_offset_t *ring; // ring buffer storing available memory
	fixedalloc_offset_t *ring_end; // end addr of ring
	fixedalloc_offset_t *ring_data_start, *ring_data_end; // start/end pos of data stored in ring
#endif
};

#endif
