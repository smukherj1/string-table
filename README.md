# string-table
Implementation of a concurrent string table. The locking uses tbb::read_write_lock which is a writer prefered spin-lock implementation. Hence, the concurrency is optimized for a higher read vs write ratio.

The allocations are done in chunks of 4KB and the string are stored in 4-byte aligned addresses.
