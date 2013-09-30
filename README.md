# FixedAlloc

Fixed memory allocator made for fast fixed-size memory allocation requiring
GCC as a compiler (using specific GCC extensions). Note that this will most
likely not work with any other compiler.

## Memory allocation tracking

There are two options for tracking available memory blocks.

### Default mode

This mode will create a data buffer to keep track of available blocks by
storing the block number of the next available blocks.

### Cell mode

Common cell based memory allocator. Each memory block is prefixed by an
information of the next available memory block. New blocks are prefixed, and
final block has a next block of -1.

