#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>

#define IN_FIXEDALLOC
#include "fixedalloc.h"

#ifdef FIXEDALLOC_DEBUG
#include <stdio.h>
#include <string.h>
#endif

#ifndef MAP_UNINITIALIZED
// this flag is not supported
#define MAP_UNINITIALIZED 0x0
#endif

static inline void fixedalloc_get_more(struct fixedalloc_data *data) {
	// allocate more memory
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: Allocating more memory for %s (currently %ld/%ld)\n", data->name, data->prealloc, data->maxalloc);
#endif
	fixedalloc_offset_t newbl[FIXEDALLOC_MORE];
	if (data->prealloc == data->maxalloc) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: %s ran out of memory\n", data->name);
#endif
		return; // can't allocate more
	}
	uintptr_t count = FIXEDALLOC_MORE;

	// check if we are about to allocate more blocks than we can
	if (count > (data->maxalloc - data->prealloc)) {
		count = data->maxalloc - data->prealloc;
	}

	for(int i = 0; i < count; i++) newbl[i] = data->prealloc + i;
	data->prealloc += count;

#ifdef FIXEDALLOC_MLOCK
	// attempt to mlock that much (failure is not fatal)
	mlock(data->memory, data->block_size * data->prealloc);
#endif /* FIXEDALLOC_MLOCK */

	// write newly available data in ring buffer
	if ((data->ring_data_end + count) <= data->ring_end) {
		// easy
		memcpy(data->ring_data_end, newbl, count*sizeof(fixedalloc_offset_t));
		data->ring_data_end += count;
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: Appended %ld blocks of memory to %s\n", count, data->name);
#endif
		return;
	}
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: Appending %ld blocks of memory to %s (reached edge of ring)\n", count, data->name);
#endif
	// need to split (reached end of ring)
	memcpy(data->ring_data_end, &newbl, (data->ring_end - data->ring_data_end) * sizeof(fixedalloc_offset_t));
	memcpy(data->ring, newbl + (data->ring_end - data->ring_data_end), (count - (data->ring_end - data->ring_data_end)) * sizeof(fixedalloc_offset_t));
	data->ring_data_end = data->ring + (count - (data->ring_end - data->ring_data_end));
}

static inline void fixedalloc_init(struct fixedalloc_data *data, uintptr_t size, uintptr_t prealloc, uintptr_t maxalloc, const char*name) {
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: initializing storage for type %s (size %lu, preallocating %lu blocks, storage at %p)\n", name, size, prealloc, data);
#endif
	data->name = name;
	data->block_size = size;
	data->allocated = 0;
	data->prealloc = prealloc;
	data->maxalloc = maxalloc;

	data->memory = mmap(NULL, size*maxalloc, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_UNINITIALIZED|MAP_HUGETLB, -1, 0);
	if ((data->memory == MAP_FAILED) && (errno = ENOSYS)) {
		// retry without MAP_HUGETLB as this kernel could possibly not support it
		data->memory = mmap(NULL, size*maxalloc, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_UNINITIALIZED, -1, 0);
	}
	if (data->memory == MAP_FAILED) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mmap memory: %s\n", strerror(errno));
#endif
		abort(); // fatal
	}
#ifdef FIXEDALLOC_MLOCK
	if (mlock(data->memory, size*prealloc) == -1) { // push [prealloc] blocks into memory for faster execution, if possible
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mlock memory for %s (not fatal, you may want to check ulimit -l): %s\n", data->name, strerror(errno));
#endif
	}
#endif /* FIXEDALLOC_MLOCK */

	uintptr_t ring_size = (size+1)*sizeof(fixedalloc_offset_t);
	data->ring = mmap(NULL, ring_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_UNINITIALIZED, -1, 0);
	if (data->ring == MAP_FAILED) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mmap ring: %s\n", strerror(errno));
#endif
		abort(); // fatal 2
	}
#ifdef FIXEDALLOC_MLOCK
	if (mlock(data->ring, ring_size) == -1) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mlock ring for %s (not fatal, you may want to check ulimit -l): %s\n", data->name, strerror(errno));
#endif
	}
#endif /* FIXEDALLOC_MLOCK */
	data->ring_data_start = data->ring_data_end = data->ring;
	data->ring_end = data->ring + ring_size;

#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: allocated buffers for %s: memory=%p ring=%p\n", data->name, data->memory, data->ring);
#endif

	// mark the blocks as available in the ring buffer :)
	for(int i = 0; i < prealloc; i++) data->ring[i] = i;
	data->ring_data_end = &data->ring[prealloc];
}

static inline void *fixedalloc_malloc(struct fixedalloc_data *data) {
	if (data->ring_data_start == data->ring_data_end) {
		// out of memory
		fixedalloc_get_more(data);
		if (data->ring_data_start == data->ring_data_end) return NULL; // out of available blocks for this
	}
	fixedalloc_offset_t off = *(data->ring_data_start++);
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: allocated block #%d from %s\n", off, data->name);
#endif
	++data->allocated;
	if (data->ring_data_start >= data->ring_end) data->ring_data_start = data->ring;
	return data->memory + (off * data->block_size);
}

static inline void fixedalloc_free(struct fixedalloc_data *data, void *ptr) {
	// compute offset
	if (ptr < data->memory) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: trying to free a block of %s outside of range\n", data->name);
#endif
		abort();
	}
	if ((ptr - data->memory) % data->block_size) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: pointer passed to free is not aligned to a %s block start\n", data->name);
#endif
		abort();
	}
	uintptr_t off = (ptr - data->memory) / data->block_size;
	if (off >= data->prealloc) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: trying to free a block of %s outside of range (offset = %ld)\n", data->name, off);
#endif
		abort();
	}
	// append back data to ring
	if (data->ring_data_end >= data->ring_end) {
		data->ring_data_end = data->ring;
	}
	--data->allocated;
	*(data->ring_data_end++) = off;
}

