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

struct fixedalloc_data {
	void *memory;
	fixedalloc_offset_t allocated; // number of currently allocated blocks
	const fixedalloc_offset_t init_prealloc; // number of currently preallocated blocks
	fixedalloc_offset_t prealloc; // number of currently preallocated blocks
	const fixedalloc_offset_t maxalloc; // number of maximum allocated blocks
	const size_t block_size; // size of one block
	const char * const name; // store name, just in case
	union {
		fixedalloc_offset_t next;
		struct {
			fixedalloc_offset_t *ring; // ring buffer storing available memory
			fixedalloc_offset_t *ring_end; // end addr of ring
			fixedalloc_offset_t *ring_data_start, *ring_data_end; // start/end pos of data stored in ring
		};
	};
};
static inline void fixedalloc_init(struct fixedalloc_data * const data) __attribute__((always_inline));
static inline void *fixedalloc_malloc(struct fixedalloc_data * const data) __attribute__((always_inline));
static inline void fixedalloc_free(struct fixedalloc_data * const data, void *ptr) __attribute__((always_inline));
static inline void fixedalloc_free_all(struct fixedalloc_data * const data) __attribute__((always_inline));

#define ALLOCATOR_DEFINE(_name, _size, _prealloc, _maxalloc) \
static struct fixedalloc_data fixedalloc_ ## _name = { .name = #_name, .block_size = _size, .allocated = 0, .init_prealloc = _prealloc, .prealloc = _prealloc, .maxalloc = _maxalloc }; \
static void init_ ## _name() __attribute__((constructor)); \
static void init_ ## _name() { fixedalloc_init(&fixedalloc_ ## _name); } \
void *malloc_ ## _name() __attribute__((malloc,hot,warn_unused_result)); \
void *malloc_ ## _name() { return fixedalloc_malloc(&fixedalloc_ ## _name); } \
void free_ ## _name(void *ptr) __attribute__((nonnull,hot)); \
void free_ ## _name(void *ptr) { fixedalloc_free(&fixedalloc_ ## _name, ptr); } \
void free_all_ ## _name() { fixedalloc_free_all(&fixedalloc_ ## _name); }
#else
#define ALLOCATOR_DEFINE(_name, _size, _prealloc, _maxalloc) \
void *malloc_ ## _name () __attribute__((malloc,hot,warn_unused_result)); \
void free_ ## _name(void*) __attribute__((nonnull,hot)); \
void free_all_ ## _name();
#endif

#include "fixedalloc_cfg.h"
