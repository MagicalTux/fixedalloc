#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h> /* memcpy */

#define IN_FIXEDALLOC
#include "fixedalloc.h"

#ifdef FIXEDALLOC_DEBUG
#include <stdio.h>
#endif

#ifndef MAP_UNINITIALIZED
// this flag is not supported
#define MAP_UNINITIALIZED 0x0
#endif

// Get a FIXEDALLOC_CTX_BLOCK_SIZE define to simplify code below
#ifdef FIXEDALLOC_CELLMODE
#define FIXEDALLOC_CTX_BLOCK_SIZE (data->block_size + sizeof(fixedalloc_offset_t))
#else
#define FIXEDALLOC_CTX_BLOCK_SIZE (data->block_size)
#endif

static inline void fixedalloc_get_more(struct fixedalloc_data *data) {
	// allocate more memory
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: Allocating more memory for %s (currently %u/%u)\n", data->name, data->prealloc, data->maxalloc);
#endif
	if (data->prealloc == data->maxalloc) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: %s ran out of memory\n", data->name);
#endif
		return; // can't allocate more
	}
	fixedalloc_offset_t count = FIXEDALLOC_MORE;

	// check if we are about to allocate more blocks than we can
	if (count > (data->maxalloc - data->prealloc)) {
		count = data->maxalloc - data->prealloc;
	}

#ifdef FIXEDALLOC_CELLMODE
#ifdef FIXEDALLOC_MLOCK
	// attempt to mlock that much (failure is not fatal)
	mlock(data->memory, FIXEDALLOC_CTX_BLOCK_SIZE * (data->prealloc+count));
	for(int i = 0; i < count; i++) {
		void *pos = data->memory + ((i+data->prealloc) * FIXEDALLOC_CTX_BLOCK_SIZE);
		if (i == count - 1) {
			// last block
			*((fixedalloc_offset_t*)pos) = data->next;
			break;
		}
		*((fixedalloc_offset_t*)pos) = i+data->prealloc+1;
	}
	data->next = data->prealloc;
	data->prealloc += count;
#endif /* FIXEDALLOC_MLOCK */

#else /* FIXEDALLOC_CELLMODE */
	fixedalloc_offset_t newbl[FIXEDALLOC_MORE];
	for(int i = 0; i < count; i++) newbl[i] = data->prealloc + i;
	data->prealloc += count;

#ifdef FIXEDALLOC_MLOCK
	// attempt to mlock that much (failure is not fatal)
	mlock(data->memory, FIXEDALLOC_CTX_BLOCK_SIZE * data->prealloc);
#endif /* FIXEDALLOC_MLOCK */

	// write newly available data in ring buffer
	if ((data->ring_data_end + count) <= data->ring_end) {
		// easy
		memcpy(data->ring_data_end, newbl, count*sizeof(fixedalloc_offset_t));
		data->ring_data_end += count;
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: Appended %d blocks of memory to %s\n", count, data->name);
#endif
		return;
	}
	// Actually we are not supposed to reach this piece of code anymore, since we prepend data to the ring too now
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: Appending %d blocks of memory to %s (reached edge of ring)\n", count, data->name);
#endif
	// need to split (reached end of ring)
	memcpy(data->ring_data_end, &newbl, (data->ring_end - data->ring_data_end) * sizeof(fixedalloc_offset_t));
	memcpy(data->ring, newbl + (data->ring_end - data->ring_data_end), (count - (data->ring_end - data->ring_data_end)) * sizeof(fixedalloc_offset_t));
	data->ring_data_end = data->ring + (count - (data->ring_end - data->ring_data_end));
#endif
}

/**
 * Initialize a fixed allocation structure
 * and allocate memory (but not really due to MAP_NORESERVE)
 */
static inline void fixedalloc_init(struct fixedalloc_data *data) {
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: initializing storage for type %s (size %ld, preallocating %d blocks, storage at %p)\n", data->name, data->block_size, data->prealloc, data);
#endif

	data->memory = mmap(NULL, FIXEDALLOC_CTX_BLOCK_SIZE * data->maxalloc, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_UNINITIALIZED|MAP_HUGETLB, -1, 0);
	if ((data->memory == MAP_FAILED) && (errno = ENOSYS)) {
		// retry without MAP_HUGETLB as this kernel could possibly not support it
		data->memory = mmap(NULL, FIXEDALLOC_CTX_BLOCK_SIZE * data->maxalloc, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_UNINITIALIZED, -1, 0);
	}
	if (data->memory == MAP_FAILED) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mmap memory: %s\n", strerror(errno));
#endif
		abort(); // fatal
	}
#ifdef FIXEDALLOC_MLOCK
	if (mlock(data->memory, FIXEDALLOC_CTX_BLOCK_SIZE * data->prealloc) == -1) { // push [prealloc] blocks into memory for faster execution, if possible
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: failed to mlock memory for %s (not fatal, you may want to check ulimit -l): %s\n", data->name, strerror(errno));
#endif
	}
#endif /* FIXEDALLOC_MLOCK */

#ifndef FIXEDALLOC_CELLMODE
	size_t ring_size = (data->block_size+1)*sizeof(fixedalloc_offset_t);
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
	for(int i = 0; i < data->prealloc; i++) data->ring[i] = i;
	data->ring_data_end = &data->ring[data->prealloc];
#else /* FIXEDALLOC_CELLMODE */
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: allocated buffers for %s: memory=%p\n", data->name, data->memory);
#endif

	// initialize the pre allocated blocks
	for(int i = 0; i < data->prealloc; i++) {
		void *pos = data->memory + (i * (size+sizeof(fixedalloc_offset_t)));
		if (i == data->prealloc - 1) {
			// last block
			*((fixedalloc_offset_t*)pos) = (fixedalloc_offset_t)-1;
			break;
		}
		*((fixedalloc_offset_t*)pos) = i+1;
	}

#endif /* !FIXEDALLOC_CELLMODE */
}

static inline void *fixedalloc_malloc(struct fixedalloc_data * const data) {
#ifdef FIXEDALLOC_CELLMODE
	if (data->next == (fixedalloc_offset_t)-1) {
		// out of memory
		fixedalloc_get_more(data);
		if (data->next == (fixedalloc_offset_t)-1) return NULL; // out of available blocks for this
	}
#ifdef FIXEDALLOC_DEBUG
	fprintf(stderr, "FixedAlloc: allocated block #%d from %s\n", data->next, data->name);
#endif
	void *ptr = data->memory + (data->next * FIXEDALLOC_CTX_BLOCK_SIZE);
	data->next = *((fixedalloc_offset_t*)ptr);
	++data->allocated;
	return ptr+sizeof(fixedalloc_offset_t);
#else /* FIXEDALLOC_CELLMODE */
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
	return data->memory + (off * FIXEDALLOC_CTX_BLOCK_SIZE);
#endif /* FIXEDALLOC_CELLMODE */
}

static inline void fixedalloc_free(struct fixedalloc_data *data, void *ptr) {
	// compute offset
	if (data->allocated == 0) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: trying to free memory in %s while all blocks are supposed to be already free\n", data->name);
#endif
		abort();
	}
#ifdef FIXEDALLOC_CELLMODE
	ptr -= sizeof(fixedalloc_offset_t);
#endif
	if (ptr < data->memory) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: trying to free a block of %s outside of range\n", data->name);
#endif
		abort();
	}
	if ((ptr - data->memory) % FIXEDALLOC_CTX_BLOCK_SIZE) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: pointer passed to free is not aligned to a %s block start\n", data->name);
#endif
		abort();
	}
	fixedalloc_offset_t off = (ptr - data->memory) / FIXEDALLOC_CTX_BLOCK_SIZE;
	if (off >= data->prealloc) {
#ifdef FIXEDALLOC_DEBUG
		fprintf(stderr, "FixedAlloc: trying to free a block of %s outside of range (offset = %u)\n", data->name, off);
#endif
		abort();
	}
#ifdef FIXEDALLOC_CELLMODE
	*((fixedalloc_offset_t*)ptr) = data->next;
	data->next = off;
#else /* FIXEDALLOC_CELLMODE */
	// prepend back data to ring, if possible
	if (data->ring_data_start > data->ring) {
		*(--data->ring_data_start) = off;
		--data->allocated;
		return;
	} else if (data->ring_data_end >= data->ring_end) {
		data->ring_data_end = data->ring;
	}
	--data->allocated;
	*(data->ring_data_end++) = off;
#endif
}

