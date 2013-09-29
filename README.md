# FixedAlloc

Fixed memory allocator made for fast fixed-size memory allocation.

## Memory allocation tracking

There are two options for tracking used memory blocks.

### Default mode

This mode will create a ring buffer to keep track of available blocks by
storing the block number of the next available blocks.

### Cell mode

Common cell based memory allocator. Each memory block is prefixed by an
information of the next available memory block. New blocks are prefixed, and
final block has a next block of -1.

