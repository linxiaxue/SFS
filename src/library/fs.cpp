// fs.cpp: File System

#include "sfs/fs.h"

#include <algorithm>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>


// Debug file system -----------------------------------------------------------

void FileSystem::debug(Disk *disk) {
    Block block;

    // Read Superblock
    disk->read(0, block.Data);

    if(block.Super.MagicNumber == MAGIC_NUMBER){
        printf("    magic number is valid\n");
    }

    printf("SuperBlock:\n");
    printf("    %u blocks\n"         , block.Super.Blocks);
    printf("    %u inode blocks\n"   , block.Super.InodeBlocks);
    printf("    %u inodes\n"         , block.Super.Inodes);

    // Read Inode blocks
    uint32_t ibnum = 1;//inode block number
    uint32_t inum = 0;//inode number, 从0开始
    Block inodeBlock;
    for(; ibnum <= block.Super.InodeBlocks; ibnum++){
        //读取第一个inode block
        disk->read(ibnum, inodeBlock.Data);
        uint32_t i = 0;
        for(; i < INODES_PER_BLOCK; i++, inum++){
            //读取inode
            Inode inode = inodeBlock.Inodes[i];
            if(inode.Valid){
                printf("Inode %u:\n", inum);
                printf("    size: %u bytes\n", inode.Size);
                uint32_t k = 0;
                printf("    direct blocks:");
                for(; k < POINTERS_PER_INODE; k++){
                    if(inode.Direct[k]){
                        printf(" %u", inode.Direct[k]);
                    }
                }
                printf("\n");
                if(inode.Indirect){
                    printf("    indirect block: %u\n");
                    printf("    indirect data blocks:", inode.Indirect);
                    Block pointerBlock;
                    disk->read(inode.Indirect, pointerBlock.Data);
                    for(k = 0; k < POINTERS_PER_BLOCK; k++){
                        if(pointerBlock.Pointers[k]){
                            printf(" %u", pointerBlock.Pointers[k]);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
}

// Format file system ----------------------------------------------------------

bool FileSystem::format(Disk *disk) {
    //不能对已挂载磁盘的文件系统格式化
    if(disk->mounted()){
        return false;
    }
    Block block = {0};
    // Write superblock
    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    //It should set aside ten percent of the blocks for inodes
    block.Super.InodeBlocks = (uint32_t)ceil((double)block.Super.Blocks * 0.1);
    block.Super.Inodes = INODES_PER_BLOCK * block.Super.InodeBlocks;
    disk->write(0, block.Data);


    // Clear all other blocks
    //准备一个空块
    memset(block.Data, 0, Disk::BLOCK_SIZE);
    //用空块覆盖其他块
    uint32_t bnum = disk->size() - 1;
    for(; bnum >= 1; bnum--){
        disk->write(bnum, block.Data);
    }
    return true;
}

// Mount file system -----------------------------------------------------------

bool FileSystem::mount(Disk *disk) {
    if(disk == currMountedDisk){
        return false;
    }
    // Read superblock
    Block superblock;
    disk->read(0, superblock.Data);
    if(superblock.Super.MagicNumber != MAGIC_NUMBER || superblock.Super.Blocks != disk->size() || superblock.Super.InodeBlocks != (uint32_t)ceil((double)superblock.Super.Blocks * 0.1) || superblock.Super.Inodes != superblock.Super.InodeBlocks * INODES_PER_BLOCK){
        return false;
    }

    // Set device and mount
    currMountedDisk = disk;
    disk->mount();

    // Copy metadata
    //设置空闲块图和inode表
    free(free_block_map);
    free(inode_table);
    free_block_map = (uint32_t *)malloc(sizeof(int) * superblock.Super.Blocks);
    inode_table = (Inode *)malloc(sizeof(Inode) * superblock.Super.Inodes);
    memset((void *)free_block_map, 0, sizeof(int) * superblock.Super.Blocks);
    memset((void *)inode_table, 0, sizeof(Inode) * superblock.Super.Inodes);
    free_block_map[0] = 1;

    uint32_t ibnum = 1;
    uint32_t inum = 0;
    Block inodeBlock;
    for(; ibnum <= superblock.Super.InodeBlocks; ibnum++){
        free_block_map[ibnum] = 1;
        uint32_t i = 0;
        disk->read(ibnum, inodeBlock.Data);
        for(; i < INODES_PER_BLOCK; i++, inum++){
            if(inodeBlock.Inodes[i].Valid && inodeBlock.Inodes[i].Size > 0){
                Inode inode = inodeBlock.Inodes[i];
                inode_table[inum] = inode;
                //将inode指向的块置1
                //直接指针
                set_free_block_map(inode.Direct, POINTERS_PER_INODE, 1);
                //间接指针
                set_free_block_map(&inode.Indirect, 1, 1);
                if(inode.Indirect){
                    Block pointerBlock;
                    disk->read(inode.Indirect, pointerBlock.Data);
                    set_free_block_map(pointerBlock.Pointers, POINTERS_PER_BLOCK, 1);
                }
            }
        }
    }

    return true;
}


//将 pointer - pointer + length 设为 value
void FileSystem::set_free_block_map(uint32_t *pointer, uint32_t length, uint32_t value){
    uint32_t i = 0;
    for(; i < length; i++){
        if(pointer[i]){
            free_block_map[pointer[i]] = value;
        }
    }
}


// Create inode ----------------------------------------------------------------

ssize_t FileSystem::create() {
    if(currMountedDisk == NULL || free_block_map == NULL || inode_table == NULL){
        return -1;
    }
    // Locate free inode in inode table
    int inum = 0;
    int inodes = (int)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;//inode table 的长度

    Inode inode;
    load_inode(0, &inode);
    //找到第一个空闲的inode
    for(; inum < inodes; inum++){
        if(!(inode_table[inum].Valid)){
            break;
        }
    }

    // Record inode if found
    if(inum < inodes){
        memset(&(inode_table[inum]), 0, sizeof(Inode));
        inode_table[inum].Valid = 1;
        if(save_inode(inum, &(inode_table[inum]))){
            return inum;
        }
    }
    return -1;
    
}

//保存inode
bool FileSystem::save_inode(size_t inumber, Inode *node){
    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return false;
    }
    Block inodeBlock;
    int ibnum = 1 + inumber/INODES_PER_BLOCK; //inode在哪一个inode block
    int index = inumber % INODES_PER_BLOCK; //在inode block中的偏移量
    currMountedDisk->read(ibnum, inodeBlock.Data);
    //在对应inode block中添加inode
    memcpy(&(inodeBlock.Inodes[index]), node, sizeof(Inode));
    currMountedDisk->write(ibnum, inodeBlock.Data);
    return true;
}

//加载inode
bool FileSystem::load_inode(size_t inumber, Inode *node){
    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return false;
    }
    Block inodeBlock;
    int ibnum = 1 + inumber/INODES_PER_BLOCK;
    int index = inumber % INODES_PER_BLOCK;
    currMountedDisk->read(ibnum, inodeBlock.Data);
    memcpy(node, &(inodeBlock.Inodes[index]), sizeof(Inode));
    return true;
}


// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {
    if(currMountedDisk == NULL || free_block_map == NULL || inode_table == NULL){
        return false;
    }

    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return false;
    }
    // Load inode information
    Inode removeInode = inode_table[inumber];
    load_inode(inumber, &removeInode);
    if(!removeInode.Valid){
        return false;
    }

    // Free direct blocks
    set_free_block_map(removeInode.Direct, POINTERS_PER_INODE, 0);

    // Free indirect blocks
    set_free_block_map(&removeInode.Indirect, 1, 0);
    if(removeInode.Indirect){
        Block pointerBlock;
        currMountedDisk->read(removeInode.Indirect, pointerBlock.Data);
        set_free_block_map(pointerBlock.Pointers, POINTERS_PER_BLOCK, 0);
    }

    // Clear inode in inode table
    memset(&(inode_table[inumber]), 0, sizeof(Inode));
    return save_inode(inumber, &(inode_table[inumber]));
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {
    if(currMountedDisk == NULL || free_block_map == NULL || inode_table == NULL){
        return -1;
    }

    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return -1;
    }
    // Load inode information
    Inode inode = inode_table[inumber];
    load_inode(inumber, &inode);
    if(!inode.Valid){
        return -1;
    }else{
        return inode.Size;
    }
    return 0;
}

// Read from inode -------------------------------------------------------------

ssize_t FileSystem::read(size_t inumber, char *data, size_t length, size_t offset) {
    if(currMountedDisk == NULL || free_block_map == NULL || inode_table == NULL){
        return -1;
    }

    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return -1;
    }

    if(length < 0){
        return -1;
    }
    // Load inode information

    Inode readInode;
    load_inode(inumber, &readInode);
    if(!readInode.Valid || offset >= readInode.Size){
        return -1;
    }
    // Adjust length
    if(length + offset > readInode.Size){
        length = readInode.Size - offset;
    }
    if(length == 0){
        return length;
    }

    // Read block and copy to data
    size_t readBytes = i_read(readInode.Direct, POINTERS_PER_INODE, length, data, offset);
    if(readBytes == length){
        return length;
    }
    load_inode(inumber, &readInode);
    //读间接块
    if(readInode.Indirect == 0){
        return -1;
    }
    if(offset <= POINTERS_PER_INODE * Disk::BLOCK_SIZE){
        offset = 0;
    }
    else{
        offset -= POINTERS_PER_INODE * Disk::BLOCK_SIZE;
    }
    Block pointersBlock;
    currMountedDisk->read(readInode.Indirect, pointersBlock.Data);
    readBytes += i_read(pointersBlock.Pointers, POINTERS_PER_BLOCK, length - readBytes, data + readBytes, offset);
    //异常情况，读完间接块还小于length
    if(readBytes < length){
        return -1;
    }

    return length;
}


size_t FileSystem::i_read(uint32_t *blockPointer, uint32_t blockNumber, size_t length, char *data, size_t offset){
    size_t readBytes = 0;
    uint32_t d = 0;
    for(; d < blockNumber; d++){
        if(blockPointer[d]){
            uint32_t bnum = blockPointer[d];
            if(offset >= (d + 1) * Disk::BLOCK_SIZE){
                //若offset超过这个块的大小
                continue;
            }
            if(offset <= d * Disk::BLOCK_SIZE && length - readBytes > Disk::BLOCK_SIZE){
                //若需要读一整个块
                currMountedDisk->read(bnum, data + readBytes);
                readBytes += Disk::BLOCK_SIZE;
            }
            else if(offset <= d * Disk::BLOCK_SIZE){
                //若只要读部分块
                Block tempBlock;
                currMountedDisk->read(bnum, tempBlock.Data);
                memcpy(data + readBytes, tempBlock.Data, length - readBytes);
                return length;
            }
            else{
                //这是第一个要读的块
                Block tempBlock;
                currMountedDisk->read(bnum, tempBlock.Data);
                if(offset + length <= (d + 1) * Disk::BLOCK_SIZE){
                    //读最后一块
                    memcpy(data + readBytes, tempBlock.Data + (offset % Disk::BLOCK_SIZE), length);
                    return length;
                }
                else{
                    memcpy(data + readBytes, tempBlock.Data + (offset % Disk::BLOCK_SIZE), (Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE)));
                    readBytes += Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE);
                }
            }
        }
    }
    return readBytes;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char *data, size_t length, size_t offset) {
    if(currMountedDisk == NULL || free_block_map == NULL || inode_table == NULL){
        return -1;
    }

    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;
    if(inumber < 0 || inumber >= inodes){
        return -1;
    }

    if(length < 0){
        return -1;
    }

    if(length == 0){
        return length;
    }

    // Load inode
    Inode writeInode;
    load_inode(inumber, &writeInode);
    if(!writeInode.Valid || offset > writeInode.Size){
        return -1;
    }
    
    // Write block and copy to data
    //写直接块
    size_t writtenBytes = i_write(writeInode.Direct,  POINTERS_PER_INODE, length, data, offset);
    if(writtenBytes == length){
        //更新inode
        writeInode.Size = offset + length;
        inode_table[inumber] = writeInode;
        save_inode(inumber, &writeInode);
        return length;
    }
    //若还没写完
    Block pointersBlock;
    if(writeInode.Indirect == 0){
        //若没有间接块，分配间接块
        ssize_t pointerBnum = allocate_free_block();
        if(pointerBnum < 0){
            //没有空闲块，更新Inode，返回
            writeInode.Size = offset + writtenBytes;
            inode_table[inumber] = writeInode;
            save_inode(inumber, &writeInode);
            return writtenBytes;
        }
        writeInode.Indirect = pointerBnum;
        memset(pointersBlock.Data, 0, Disk::BLOCK_SIZE);
    }
    else{
        currMountedDisk->read(writeInode.Indirect, pointersBlock.Data);
    }
    //写间接块
    size_t newOffset = 0;
    if(offset > POINTERS_PER_INODE * Disk::BLOCK_SIZE){
        newOffset = offset - POINTERS_PER_INODE * Disk::BLOCK_SIZE;
    }
    writtenBytes += i_write(pointersBlock.Pointers, POINTERS_PER_BLOCK, length - writtenBytes, data + writtenBytes, newOffset);
    //更新inode
    writeInode.Size = offset + writtenBytes;
    inode_table[inumber] = writeInode;
    save_inode(inumber, &writeInode);
    currMountedDisk->write(writeInode.Indirect, pointersBlock.Data);
    return writtenBytes;
    
}


size_t FileSystem::i_write(uint32_t *bnumPointer, uint32_t bnumNumber, size_t length, char *data, size_t offset){
    size_t writtenBytes = 0;
    uint32_t d = 0;
    for(; d < bnumNumber; d++){
        if(offset >= (d + 1) * Disk::BLOCK_SIZE){
            continue;
        }
        //先读出要写的块
        Block block;
        if(bnumPointer[d]){
            currMountedDisk->read(bnumPointer[d], block.Data);
        }
        else{
            //写新块
            //分配一个新块
            ssize_t newBnum = allocate_free_block();
            if(newBnum < 0){
                //没有剩余的空闲块
                return writtenBytes;
            }
            bnumPointer[d] = newBnum;
            currMountedDisk->read(newBnum, block.Data);
        }
        if(offset <= d * Disk::BLOCK_SIZE && length - writtenBytes > Disk::BLOCK_SIZE){
            //写整个块
            memcpy(block.Data, data + writtenBytes, Disk::BLOCK_SIZE);
            currMountedDisk->write(bnumPointer[d], block.Data);
            writtenBytes += Disk::BLOCK_SIZE;
        }
        else if(offset <= d * Disk::BLOCK_SIZE){
            //写部分块
            memcpy(block.Data, data + writtenBytes, length - writtenBytes);
            currMountedDisk->write(bnumPointer[d], block.Data);
            return length;
        }
        else{
            //处理要写的第一个块
            if(offset + length <= (d + 1) * Disk::BLOCK_SIZE){
                memcpy(block.Data + (offset % Disk::BLOCK_SIZE), data + writtenBytes, length);
                currMountedDisk->write(bnumPointer[d], block.Data);
                return length;
            }
            else{
                memcpy(block.Data + (offset % Disk::BLOCK_SIZE), data + writtenBytes, (Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE)));
                currMountedDisk->write(bnumPointer[d], block.Data);
                writtenBytes += Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE);
            }
        }
    }
    return writtenBytes;
}

//分配一个空闲块
ssize_t FileSystem::allocate_free_block(){
    size_t bnum = (size_t)ceil((double)currMountedDisk->size() * 0.1) + 1;
    for(; bnum < currMountedDisk->size(); bnum++){
        if(free_block_map[bnum] == 0){
            free_block_map[bnum] = 1;
            return bnum;
        }
    }
    printf("disk is full.\n");
    return -1;
}


FileSystem::~FileSystem(){
        free(free_block_map);
        free(inode_table);
}
