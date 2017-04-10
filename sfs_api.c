#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sfs_api.h"

int write_FBM(int index, int value) ;
int read_FBM(int index);

char * DISK_NAME = "mySSFS";
short MY_BLOCK_SIZE = 1024;
short MY_NUM_BLOCKS = 1024;
short NUM_INODES = 74;

short MAX_FILENAME = 10;
short MAX_FILESIZE = 28672;

short NUM_DIRECT_PTRS = 14;

short SUPERBLOCK_NO = 0;

typedef struct {
    short size;
    short blockPtrs[14]; // Must be same value as NUM_DIRECT_PTRS
    short indirectPtr;
} inode_t;

typedef struct {
    char name[10]; // Must be same value as MAX_FILENAME
    short iNodeNo;
} d_entry_t;

typedef struct {
    short magicValue; // Represents number of I-node blocks
    short blockSize;
    short fileSystemSize;
    short numberOfInodes;

    int fbm[32];

    d_entry_t files[32];

} superblock_t;


typedef struct {
    short inodeNo;
    short readPtr, writePtr;
} fd_table_entry_t;

superblock_t * superBlock; // Keep this block in RAM

inode_t inodes[74];// Useful to have quick access to iNodes as well (array size must be same value as NUM_INODES)

fd_table_entry_t openFD[32]; // Max of 32 files open at once

/**
 * Gets a shadow File system partition. Also initializes the open file descriptor table
 * @param fresh 0 retrieves the initialized file system, 1 re-initializes the file system
 */
void mkssfs(int fresh){

    superBlock = calloc(sizeof(superblock_t), 1);
    memset(openFD, -1, sizeof(openFD));

    if(fresh) {
        init_fresh_disk(DISK_NAME, MY_BLOCK_SIZE, MY_NUM_BLOCKS);

        // Initialize superblock metadata
        superBlock->magicValue = (short) ceil(((double) NUM_INODES) / (MY_BLOCK_SIZE / sizeof(inode_t)));
        superBlock->blockSize = MY_BLOCK_SIZE;
        superBlock->fileSystemSize = MY_NUM_BLOCKS;
        superBlock->numberOfInodes = NUM_INODES;


        // Initialize fbm and directory table and inodes
        memset(&superBlock->fbm, -1, sizeof(superBlock->fbm));
        memset(superBlock->files, -1, sizeof(superBlock->files));
        memset(inodes, -1, sizeof(inodes));

        // Set the free bits to 0
        for(int i = SUPERBLOCK_NO + 1; i < SUPERBLOCK_NO + 1 + superBlock->magicValue; i++) {
            write_FBM(i, 0);
        }

        write_blocks(SUPERBLOCK_NO, 1, superBlock);
        write_blocks(SUPERBLOCK_NO+1, superBlock->magicValue, inodes);


    } else {
        init_disk(DISK_NAME, MY_BLOCK_SIZE, MY_NUM_BLOCKS);

        // Temp block buffer
        void * blockRead = malloc(MY_BLOCK_SIZE);

        // Get superblock
        read_blocks(SUPERBLOCK_NO, 1, blockRead);
        memcpy(superBlock, blockRead, sizeof(superblock_t));

        free(blockRead);

        // Get inodes
        blockRead = malloc(MY_BLOCK_SIZE * superBlock->magicValue);
        read_blocks(SUPERBLOCK_NO + 1, superBlock->magicValue, blockRead);
        memcpy(inodes, blockRead, sizeof(inodes));

        free(blockRead);

    }

}

/**
 * Finds an unused block, updates FBM and returns block number
 * @return the block number, or -1 no more blocks available
 */
short allocate_block() {

    short blockIndex = (short) (SUPERBLOCK_NO + 1 + superBlock->magicValue);
    while( read_FBM(blockIndex) != 1 && blockIndex < MY_NUM_BLOCKS)
        blockIndex++;

    // No more available blocks
    if(blockIndex >= MY_NUM_BLOCKS)
        return -1;

    // Mark block as used
    write_FBM(blockIndex, 0);

    // Update superblock
    write_blocks(SUPERBLOCK_NO, 1, superBlock);

    return blockIndex;
}

/**
 * Frees an block by it's index in the filesystem.
 * @param blockNo
 * @return 0 on success, -1 on failure
 */
short free_block(short blockNo) {

    // Check if block is in bounds
    if(blockNo <= superBlock->magicValue || blockNo >= MY_NUM_BLOCKS)
        return -1;

    // Mark block as unused
    write_FBM(blockNo, 1);

    // Update superblock
    write_blocks(SUPERBLOCK_NO, 1, superBlock);

    return 0;
}

/**
 * Finds an unused i-node, sets it's size to 0 and updates i-node file
 * @return the allocated iNodeNo or -1 for no more inodes
 */
short allocate_iNode() {

    short iNodeIndex = 0;
    while( inodes[iNodeIndex].size != -1 && iNodeIndex < NUM_INODES)
        iNodeIndex++;

    // No more available iNodes
    if( iNodeIndex >= NUM_INODES)
        return -1;

    // Set size to 0 to mark as used
    inodes[iNodeIndex].size = 0;

    // Update iNode file
    write_blocks(SUPERBLOCK_NO + 1, superBlock->magicValue, inodes);

    return iNodeIndex;
}

/**
 * Frees an i-node by it's index in the i-node file. Also frees any associated direct blocks and indirect blocks
 * @param inodeNo
 * @return 0 on success, -1 on failure
 */
short free_iNode(short inodeNo) {

    // Check if inodeNo is in bounds
    if(inodeNo < 0 || inodeNo >= NUM_INODES)
        return -1;

    inode_t * currentINode = &inodes[inodeNo];
    // Set inode size to -1
    currentINode->size = -1;

    short singleIndirectsLeft = 1; // Safety check to only go one indirect deep

    // Loop through and free any allocated blocks
    short blockPtrIndex = 0;
    while(currentINode->blockPtrs[blockPtrIndex] != -1) {
        free_block(currentINode->blockPtrs[blockPtrIndex]);

        // If at end of i-node
        if (blockPtrIndex >= NUM_DIRECT_PTRS) {

            // Switch to indirect if not done already
            if(currentINode->indirectPtr != -1 && singleIndirectsLeft > 0) {
                currentINode = &inodes[currentINode->indirectPtr];
                currentINode->size = -1;
                singleIndirectsLeft--;
                blockPtrIndex = 0;
            }
            else break;
        }

        // Clear the block pointer
        currentINode->blockPtrs[blockPtrIndex] = -1;
        blockPtrIndex++;
    }

    // Update iNode file
    write_blocks(SUPERBLOCK_NO + 1, superBlock->magicValue, inodes);

    return 0;
}

/**
 * Creates a new file by allocating an unused iNode and block then adding the file to the directory table
 * @return the index in the directory table or -1 if directory full, -2 no unsued inodes available, -3 for no more blocks available
 */
short ssfs_fcreate(char *name) {

    // Find an open entry in the directory table
    short dTableIndex = 0;
    int numFileEntries = sizeof(superBlock->files)/sizeof(superBlock->files[0]);
    while( superBlock->files[dTableIndex].iNodeNo != -1  && dTableIndex < numFileEntries)
        dTableIndex++;

    // No empty locations, return -1
    if (dTableIndex >= numFileEntries)
        return -1;

    // Get an i-node
    short iNodeNo = allocate_iNode();

    // No more iNodes
    if(iNodeNo == -1)
        return -2;

    // Allocate a block
    inodes[iNodeNo].blockPtrs[0] = allocate_block();

    // Put info in directory table
    superBlock->files[dTableIndex].iNodeNo = iNodeNo;
    strcpy(superBlock->files[dTableIndex].name, name);

    // Update superblock
    write_blocks(SUPERBLOCK_NO, 1, superBlock);

    // Update i-nodes file
    write_blocks(SUPERBLOCK_NO + 1, superBlock->magicValue, inodes);

    return dTableIndex;
}

/**
 * Opens a file by name, or create one if it does not exist returning it's file descriptor
 * @param name The name of the file to open or create
 * @return The index of the file descriptor in the open file descriptor table or a negative number indicating an error
 */
int ssfs_fopen(char *name){

    // Check for null pointer or null name
    if( name == NULL || *name == '\0')
        return -1;

    // Truncate name if necessary
    char * shortName;

    if( strlen(name) > MAX_FILENAME) {
        shortName = malloc(MAX_FILENAME * sizeof(char));
        memcpy(shortName, name, MAX_FILENAME);
    } else {
        shortName = name;
    }

    // Search for file entry if it exists
    int dTableIndex = 0;
    int numFileEntries = sizeof(superBlock->files)/sizeof(superBlock->files[0]);
    while(strcmp(name, superBlock->files[dTableIndex].name) !=0  && dTableIndex < numFileEntries)
        dTableIndex++;

    // Create file if it doesn't exist
    if(dTableIndex >= numFileEntries) {
        dTableIndex = ssfs_fcreate(name);

        // Return if error
        if(dTableIndex < 0)
            return dTableIndex;
    }

    // Check to see if file is already open
    int openFDIndex = 0;
    int numFDEntries = sizeof(openFD)/sizeof(openFD[0]);
    while(openFDIndex < numFDEntries) {
        if(openFD[openFDIndex].inodeNo == superBlock->files[dTableIndex].iNodeNo)
            return openFDIndex;

        openFDIndex++;
    }

    // Otherwise search for first free entry in openFD
    openFDIndex = 0;
    while(openFD[openFDIndex].inodeNo != -1 && openFDIndex < numFDEntries)
        openFDIndex++;

    // Return -1 if no more available entries
    if(openFDIndex >= numFDEntries) {
        return -1;
    }

    // Set the inodeNo and pointers
    openFD[openFDIndex].inodeNo = superBlock->files[dTableIndex].iNodeNo;
    openFD[openFDIndex].readPtr = 0;
    openFD[openFDIndex].writePtr = inodes[openFD[openFDIndex].inodeNo].size;

    return openFDIndex;
}

/**
 * Remove an entry from the open file descriptor table
 * @param fileID the index to remove
 * @return 0 on success, -1 on index out of bounds, -2 if fileID points to an empty slot
 */
int ssfs_fclose(int fileID){

    // Index out of bounds
    if(fileID < 0 || fileID >= sizeof(openFD)/ sizeof(openFD[0]))
        return -1;

    // Check if fileID points to an empty location
    if(openFD[fileID].inodeNo == -1)
        return -2;

    // Remove entry by setting params to -1
    openFD[fileID].inodeNo = -1;
    openFD[fileID].readPtr = -1;
    openFD[fileID].writePtr = -1;

    return 0;
}

/**
 * Moves the read pointer of an open file by ID
 * @param fileID
 * @param loc Location to move the read pointer to
 * @return 0 on success, -1 on failure
 */
int ssfs_frseek(int fileID, int loc){

    // Check if fileID is in bounds and valid
    if(fileID < 0 || fileID >= sizeof(openFD)/ sizeof(openFD[0]) || openFD[fileID].inodeNo == -1)
        return -1;

    // Check if location is valid
    if(loc < 0 || loc >= inodes[openFD[fileID].inodeNo].size)
        return -1;

    openFD[fileID].readPtr = (short) loc;

    return 0;
}

/**
 * Moves the write pointer of an open file by ID
 * @param fileID
 * @param loc Location to move the write pointer to
 * @return 0 on success, -1 on failure
 */
int ssfs_fwseek(int fileID, int loc){

    // Check if fileID is in bounds and valid
    if(fileID < 0 || fileID >= sizeof(openFD)/ sizeof(openFD[0]) || openFD[fileID].inodeNo == -1)
        return -1;

    // Check if location is valid
    if(loc < 0 || loc > inodes[openFD[fileID].inodeNo].size) // Note loc can be max one byte after end of file
        return -1;

    openFD[fileID].writePtr = (short) loc;

    return 0;
}

int ssfs_fwrite(int fileID, char *buf, int length){

    // Check if fileID is in bounds and valid
    if(fileID < 0 || fileID >= sizeof(openFD)/ sizeof(openFD[0]) || openFD[fileID].inodeNo == -1)
        return -1;

    // Check if buf pointer not null and length is positive
    if(buf == NULL || length < 0)
        return -1;

    inode_t * directINode = &inodes[openFD[fileID].inodeNo];
    short fileSize = directINode->size;
    short writePtr = openFD[fileID].writePtr;

    // Check if length is too large, else truncate
    if(length + writePtr > MAX_FILESIZE)
        length = MAX_FILESIZE - writePtr;

    inode_t * currentINode = directINode;
    
    // Compute where to start writing
    short startBlockPtr = writePtr / MY_BLOCK_SIZE;

    // Determine if jump straight to indirect is necessary
    if(startBlockPtr >= NUM_DIRECT_PTRS) {
        //startIndirect = 1;
        currentINode = &inodes[directINode->indirectPtr];
        startBlockPtr -= NUM_DIRECT_PTRS;
    }

    short bytesLeft = (short) length;
    short i = startBlockPtr;

    // Temp block buffer
    void * blockRead = malloc(MY_BLOCK_SIZE);

    // Continue writing until no bytes left
    while(bytesLeft > 0) {

        //Allocate block if neccesary
        if(currentINode->blockPtrs[i] == -1) {
            currentINode->blockPtrs[i] = allocate_block();

            // Save i-node file
            write_blocks(SUPERBLOCK_NO + 1, superBlock->magicValue, inodes);

            //Error
            if(currentINode->blockPtrs[i] < 0)
                return -1;
        }

        // Read block, change data and write back
        read_blocks(currentINode->blockPtrs[i], 1, blockRead);

        // Determine how many bytes to write in current block
        short bytesLeftInBlock = MY_BLOCK_SIZE - (writePtr % MY_BLOCK_SIZE);

        if( (writePtr % MY_BLOCK_SIZE) + bytesLeft < MY_BLOCK_SIZE) {
            bytesLeftInBlock = bytesLeft;
        }

        // Copy from buffer to block
        memcpy(blockRead + (writePtr % MY_BLOCK_SIZE), buf, bytesLeftInBlock);

        write_blocks(currentINode->blockPtrs[i], 1, blockRead);

        // Move along buffer, decrease remaining length and update write pointer
        buf += bytesLeftInBlock;
        bytesLeft -= bytesLeftInBlock;
        writePtr += bytesLeftInBlock;
        i++;

        // Move to indirect
        if(i >= NUM_DIRECT_PTRS) {
            i = 0;
            currentINode = &inodes[directINode->indirectPtr];
        }

    }
    /*
    if(!startIndirect) {
        // Start writing to direct first
        while(bytesLeft > 0 && i < NUM_DIRECT_PTRS) {

            //Allocate block if neccesary
            if(directINode->blockPtrs[i] == -1) {
                directINode->blockPtrs[i] = allocate_block();

                //Error
                if(directINode->blockPtrs[i] < 0)
                    return -1;
            }

            write_blocks(directINode->blockPtrs[i], 1, buf);

            // Move along buffer and decrease remaining length
            buf += MY_BLOCK_SIZE;
            bytesLeft -= MY_BLOCK_SIZE;
            i++;
        }

        // Start writing to indirect if bytes remain
        if(bytesLeft > 0) {
            startIndirect = 1;
            i = 0;
        }
    }

    if(startIndirect) {
        // Write straight to indirect node
        inode_t * indirectINode = &inodes[directINode->indirectPtr];
        while(bytesLeft > 0) {

            // Allocate block if neccesary
            if(indirectINode->blockPtrs[i] == -1) {
                indirectINode->blockPtrs[i] = allocate_block();

                //Error
                if(directINode->blockPtrs[i] < 0)
                    return -1;
            }

            write_blocks(indirectINode->blockPtrs[i], 1, buf);

            // Move along buffer and decrease remaining length
            buf += MY_BLOCK_SIZE;
            bytesLeft -= MY_BLOCK_SIZE;
            i++;
        }
    }
     */

    free(blockRead);

    // Save write pointer
    openFD[fileID].writePtr = writePtr;

    // Update file size if neccesary
    if(openFD[fileID].writePtr > fileSize)
        directINode->size = openFD[fileID].writePtr;

    return length;
}

int ssfs_fread(int fileID, char *buf, int length){

    // Check if fileID is in bounds and valid
    if(fileID < 0 || fileID >= sizeof(openFD)/ sizeof(openFD[0]) || openFD[fileID].inodeNo == -1)
        return -1;

    // Check if buf pointer not null and length is positive
    if(buf == NULL || length < 0)
        return -1;

    inode_t * directINode = &inodes[openFD[fileID].inodeNo];
    short fileSize = directINode->size;
    short readPtr = openFD[fileID].readPtr;

    // Check if length is too large, else truncate
    if(length + readPtr > MAX_FILESIZE)
        length = MAX_FILESIZE - readPtr;

    inode_t * currentINode = directINode;

    // Compute where to start reading
    short startBlockPtr = readPtr / MY_BLOCK_SIZE;

    if(startBlockPtr >= NUM_DIRECT_PTRS) {
        currentINode = &inodes[directINode->indirectPtr];
        startBlockPtr -= NUM_DIRECT_PTRS;
    }

    short bytesLeft = (short) length;
    short i = startBlockPtr;

    // Temp block buffer
    void * blockRead = malloc(MY_BLOCK_SIZE);
    
    // Continue reading until no bytes left
    while(bytesLeft > 0) {
        
        read_blocks(currentINode->blockPtrs[i], 1, blockRead);

        // Determine how many bytes to read in current block
        short bytesLeftInBlock = MY_BLOCK_SIZE - (readPtr % MY_BLOCK_SIZE);

        if( (readPtr % MY_BLOCK_SIZE) + bytesLeft < MY_BLOCK_SIZE) {
            bytesLeftInBlock = bytesLeft;
        }

        // Copy from block to buffer
        memcpy(buf, blockRead + (readPtr % MY_BLOCK_SIZE), bytesLeftInBlock);

        // Move along buffer, decrease remaining length and update read pointer
        buf += bytesLeftInBlock;
        bytesLeft -= bytesLeftInBlock;
        readPtr += bytesLeftInBlock;
        i++;

        // Move to indirect
        if(i >= NUM_DIRECT_PTRS) {
            i = 0;
            currentINode = &inodes[directINode->indirectPtr];
        }
    }
    
    free(blockRead);
    
    // Save read pointer
    openFD[fileID].readPtr = readPtr;
    
    return length;
}

int ssfs_remove(char *file){

    // Check for null pointer or null name
    if( file == NULL || *file == '\0')
        return -1;

    // Truncate name if necessary
    char * shortName;

    if( strlen(file) > MAX_FILENAME) {
        shortName = malloc(MAX_FILENAME * sizeof(char));
        memcpy(shortName, file, MAX_FILENAME);
    } else {
        shortName = file;
    }

    // Search for file entry if it exists
    int dTableIndex = 0;
    int numFileEntries = sizeof(superBlock->files)/sizeof(superBlock->files[0]);
    while(strcmp(file, superBlock->files[dTableIndex].name) !=0  && dTableIndex < numFileEntries)
        dTableIndex++;

    // Return error if it doesn't exist
    if(dTableIndex >= numFileEntries)
        return -1;

    // Close file first if it is open
    int openFDIndex = 0;
    int numFDEntries = sizeof(openFD)/sizeof(openFD[0]);
    while(openFDIndex < numFDEntries) {
        if(openFD[openFDIndex].inodeNo == superBlock->files[dTableIndex].iNodeNo) {
            ssfs_fclose(openFDIndex);
            break;
        }
        openFDIndex++;
    }

    // Get i-node and start freeing up blocks
    free_iNode(superBlock->files[dTableIndex].iNodeNo);

    // Free entry in directory table
    memset(&superBlock->files[dTableIndex], -1, sizeof(d_entry_t));

    // Update superblock
    write_blocks(SUPERBLOCK_NO, 1, superBlock);

    return 0;
}

/**
 * Read a bit from the FBM at specified index
 * Cannot access the bit for superblock or the FBM itself
 *
 * @param index
 * @return bit at index on success, -1 if error (out of bounds)
 */
int read_FBM(int index) {

    // Index out of bounds
    if(index >= MY_NUM_BLOCKS || index < 0 || index == SUPERBLOCK_NO)
        return -1;

    int intNo = index / (sizeof(superBlock->fbm)/ sizeof(int));
    int selectedInt = superBlock->fbm[intNo];
    int bitNo = index % 32;

    return (selectedInt >> bitNo) & 1;
}

/**
 * Write a bit to the FBM at specified index
 * Cannot write to the bit for the superblock or the FBM itself
 *
 * @param index
 * @param value
 * @return 0 on successs, -1 if error (out of bounds or invalid value)
 */
int write_FBM(int index, int value) {

    // Index out of bounds or invalid value
    if(index >= MY_NUM_BLOCKS || index < 0 || index == SUPERBLOCK_NO
        || value < 0 || value > 1)
        return -1;

    int intNo = index / (sizeof(superBlock->fbm)/ sizeof(int));
    int * selectedInt = &superBlock->fbm[intNo];
    int bitNo = index % 32;

    // Obtained from http://stackoverflow.com/questions/47981/how-do-you-set-clear-and-toggle-a-single-bit-in-c-c
    *selectedInt ^= (-value ^ *selectedInt) & (1 << bitNo);

    // Update superblock
    write_blocks(SUPERBLOCK_NO, 1, superBlock);

    return 0;
}
//
//int main() {
//
//    mkssfs(1);
//
//    write_FBM(62, 1);
//    write_FBM(62, 0);
//    write_FBM(62, 0);
//    write_FBM(62, 1);
//    return 0;
//}