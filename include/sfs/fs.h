// fs.h: File System

#pragma once

#include "sfs/disk.h"

#include <stdint.h>

class FileSystem {
public:
    const static uint32_t MAGIC_NUMBER	     = 0xf0f03410;
    const static uint32_t INODES_PER_BLOCK   = 128;
    const static uint32_t POINTERS_PER_INODE = 5;
    const static uint32_t POINTERS_PER_BLOCK = 1024;

private:
    struct SuperBlock {		// Superblock structure
    	uint32_t MagicNumber;	// File system magic number
    	uint32_t Blocks;	// Number of blocks in file system
    	uint32_t InodeBlocks;	// Number of blocks reserved for inodes
    	uint32_t Inodes;	// Number of inodes in file system
    };

    struct Inode {
    	uint32_t Valid;		// Whether or not inode is valid
    	uint32_t Size;		// Size of file
    	uint32_t Direct[POINTERS_PER_INODE]; // Direct pointers
    	uint32_t Indirect;	// Indirect pointer
    };

    union Block {
    	SuperBlock  Super;			    // Superblock
    	Inode	    Inodes[INODES_PER_BLOCK];	    // Inode block
    	uint32_t    Pointers[POINTERS_PER_BLOCK];   // Pointer block
    	char	    Data[Disk::BLOCK_SIZE];	    // Data block
    };

    // TODO: Internal helper functions
    void set_free_block_map(uint32_t *pointer, uint32_t length, uint32_t value);
    bool load_inode(size_t inumber, Inode *node);
    bool save_inode(size_t inumber, Inode *node);
    size_t i_read(uint32_t *bnumPointer, uint32_t bnumNumber, size_t length, char *data, size_t offset);
    size_t i_write(uint32_t *bnumPointer, uint32_t bnumNumber, size_t length, char *data, size_t offset);
    ssize_t allocate_free_block();

    // TODO: Internal member variables
    Disk *currMountedDisk = NULL;  //当前挂载的磁盘
    Inode *inode_table = NULL;    //inode表
    uint32_t *free_block_map = NULL;    //空闲块图


public:
    static void debug(Disk *disk);
    static bool format(Disk *disk);

    bool mount(Disk *disk);

    ssize_t create();
    bool    remove(size_t inumber);
    ssize_t stat(size_t inumber);

    ssize_t read(size_t inumber, char *data, size_t length, size_t offset);
    ssize_t write(size_t inumber, char *data, size_t length, size_t offset);
};
