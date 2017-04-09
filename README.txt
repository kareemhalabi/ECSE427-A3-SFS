Notes on my submission and reasoning for design choices:

1. Since I am not attempting commit restore, the root j-node and shadow nodes are unnecessary
since we can simply store all the inodes after the superblock in the traditional UNIX fashion.
Thus a lot of space would normally be unused in the superblock which I will utilize to store
the metadata, FBM and the root directory table.

With a 1024 sized block the superblock will contain:
-8 bytes of metadata
-128 bytes for FBM
-888 bytes for directory table
   -12 byte directory table entry gives
   -Max 74 entries with average file size of ((1024 * 1023)/74) = 14,156 bytes
   (I only end up using the default of 32 entries in test.c)


2. Due to the limit on the size of a block and the number of blocks,
the size of an i-node can be reduced significantly

-The size variable can be 2 bytes (signed) allowing for theoretical max file size of (2^15 - 1) = 32,767 bytes
-Each pointer need only be 2 bytes long to be able to address all 1024 blocks
-The indirect can also be 2 bytes long for consistency.

I chose to have 32 byte i-nodes so there are 14 direct pointers and 1 single indirect
allowing for actual max file size of 14*2*1024 = 28,672 bytes.
(This is slighly less than the default of 30000 in test.c)

Thus each block can contain (1024/32) =  32 i-nodes. To be able to address each block we need (1024/14) = 74 i-nodes.
74 i-nodes fit in 3 blocks