#include "fs.h"
#include "disk.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* Constants */
#define MAX_FILES 64
#define MAX_FILENAME_LENGTH 16
#define MAX_OPEN_FILES 32
#define BLOCK_SIZE 4096
#define MAX_INODE_DIRECT_OFFSETS 12
#define BITMAP_SIZE (DISK_BLOCKS / 8)
#define BLOCK_ENTRIES (BLOCK_SIZE / sizeof(uint16_t))

/* Block Offsets */
#define SUPERBLOCK_OFFSET 0
#define INODE_TABLE_OFFSET 1
#define DIRECTORY_TABLE_OFFSET 2
#define BITMAP_OFFSET 3
#define DATA_BLOCKS_OFFSET 4

/* Structures */
typedef struct {
    uint16_t inode_table_size;
    uint16_t inode_table_offset;
    uint16_t directory_table_offset;
    uint16_t bitmap_offset;
    uint16_t data_block_offset;
} Superblock;

typedef struct {
    bool is_allocated;
    uint16_t direct_blocks[MAX_INODE_DIRECT_OFFSETS];
    uint16_t indirect_block;
    uint32_t size;
} Inode;

typedef struct {
    bool is_allocated;
    char name[MAX_FILENAME_LENGTH];
    uint16_t inode_index;
} DirectoryEntry;

typedef struct {
    bool in_use;
    uint16_t inode_index;
    uint32_t offset;
} FileDescriptor;

/* Globals */
static Superblock superblock;
static Inode inode_table[MAX_FILES];
static DirectoryEntry root_dir[MAX_FILES];
static FileDescriptor open_files[MAX_OPEN_FILES];
static uint8_t bitmap[BITMAP_SIZE];

/* Helper Functions */
static bool is_bit_set(uint8_t *map, int index) {
    return (map[index / 8] & (1 << (index % 8))) != 0;
}

static void set_bit(uint8_t *map, int index) {
    map[index / 8] |= (1 << (index % 8));
}

static void clear_bit(uint8_t *map, int index) {
    map[index / 8] &= ~(1 << (index % 8));
}

static int find_free_bit(uint8_t *map, int max_bits) {
    for (int i = 0; i < max_bits; i++) {
        if (!is_bit_set(map, i)) {
            return i;
        }
    }
    return -1;
}

/* Block Management */
static uint16_t allocate_block() {
    int free_block = find_free_bit(bitmap, DISK_BLOCKS);
    if (free_block == -1) {
        return 0; // No free blocks available
    }
    set_bit(bitmap, free_block);
    return (uint16_t)free_block;
}

static void free_block(uint16_t block_num) {
    clear_bit(bitmap, block_num);
}

/* Initialization Functions */
int make_fs(char *disk_name) {
    if (make_disk(disk_name) != 0 || open_disk(disk_name) != 0) {
        return -1;
    }

    char block[BLOCK_SIZE] = {0};

    /* Initialize Superblock */
    superblock.inode_table_size = MAX_FILES;
    superblock.inode_table_offset = INODE_TABLE_OFFSET;
    superblock.directory_table_offset = DIRECTORY_TABLE_OFFSET;
    superblock.bitmap_offset = BITMAP_OFFSET;
    superblock.data_block_offset = DATA_BLOCKS_OFFSET;
    memcpy(block, &superblock, sizeof(Superblock));
    if (block_write(SUPERBLOCK_OFFSET, block) < 0) {
        return -1;
    }

    /* Initialize Inodes */
    memset(inode_table, 0, sizeof(inode_table));
    memcpy(block, inode_table, sizeof(inode_table));
    if (block_write(INODE_TABLE_OFFSET, block) < 0) {
        return -1;
    }

    /* Initialize Directory */
    memset(root_dir, 0, sizeof(root_dir));
    memcpy(block, root_dir, sizeof(root_dir));
    if (block_write(DIRECTORY_TABLE_OFFSET, block) < 0) {
        return -1;
    }

    /* Initialize Bitmap */
    memset(bitmap, 0, sizeof(bitmap));
    memcpy(block, bitmap, sizeof(bitmap));
    if (block_write(BITMAP_OFFSET, block) < 0) {
        return -1;
    }

    return close_disk();
}

int mount_fs(char *disk_name) {
    if (open_disk(disk_name) != 0) {
        return -1;
    }

    char block[BLOCK_SIZE] = {0};

    /* Load Superblock */
    if (block_read(SUPERBLOCK_OFFSET, block) < 0) {
        return -1;
    }
    memcpy(&superblock, block, sizeof(Superblock));

    /* Load Inodes */
    if (block_read(INODE_TABLE_OFFSET, block) < 0) {
        return -1;
    }
    memcpy(inode_table, block, sizeof(inode_table));

    /* Load Directory */
    if (block_read(DIRECTORY_TABLE_OFFSET, block) < 0) {
        return -1;
    }
    memcpy(root_dir, block, sizeof(root_dir));

    /* Load Bitmap */
    if (block_read(BITMAP_OFFSET, block) < 0) {
        return -1;
    }
    memcpy(bitmap, block, sizeof(bitmap));

    /* Reset File Descriptor Table */
    memset(open_files, 0, sizeof(open_files));
    return 0;
}

int umount_fs(char *disk_name) {
    char block[BLOCK_SIZE] = {0};

    /* Save Bitmap */
    memcpy(block, bitmap, sizeof(bitmap));
    if (block_write(BITMAP_OFFSET, block) < 0) {
        return -1;
    }

    /* Save Directory */
    memcpy(block, root_dir, sizeof(root_dir));
    if (block_write(DIRECTORY_TABLE_OFFSET, block) < 0) {
        return -1;
    }

    /* Save Inodes */
    memcpy(block, inode_table, sizeof(inode_table));
    if (block_write(INODE_TABLE_OFFSET, block) < 0) {
        return -1;
    }

    /* Save Superblock */
    memcpy(block, &superblock, sizeof(Superblock));
    if (block_write(SUPERBLOCK_OFFSET, block) < 0) {
        return -1;
    }

    return close_disk();
}

/* File System Operations */

int fs_open(char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].is_allocated && strcmp(root_dir[i].name, name) == 0) {
            for (int j = 0; j < MAX_OPEN_FILES; j++) {
                if (!open_files[j].in_use) {
                    open_files[j].in_use = true;
                    open_files[j].inode_index = root_dir[i].inode_index;
                    open_files[j].offset = 0;
                    return j; // Return the file descriptor
                }
            }
            return -1; // No available file descriptor
        }
    }
    return -1; // File not found
}

int fs_close(int fildes) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    open_files[fildes].in_use = false;
    open_files[fildes].inode_index = 0;
    open_files[fildes].offset = 0;
    return 0; // Success
}

int fs_create(char *name) {
    if (strlen(name) > MAX_FILENAME_LENGTH || strlen(name) == 0) {
        return -1; // Invalid name length
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].is_allocated && strcmp(root_dir[i].name, name) == 0) {
            return -1; // File already exists
        }
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inode_table[i].is_allocated) {
            inode_table[i].is_allocated = true;
            inode_table[i].size = 0;
            memset(inode_table[i].direct_blocks, 0, sizeof(inode_table[i].direct_blocks));
            inode_table[i].indirect_block = 0;

            for (int j = 0; j < MAX_FILES; j++) {
                if (!root_dir[j].is_allocated) {
                    root_dir[j].is_allocated = true;
                    root_dir[j].inode_index = i;
                    strncpy(root_dir[j].name, name, MAX_FILENAME_LENGTH);
                    return 0; // Success
                }
            }
            inode_table[i].is_allocated = false; // Revert inode if no directory slot is found
            break;
        }
    }
    return -1; // No available inodes or directory slots
}

int fs_delete(char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].is_allocated && strcmp(root_dir[i].name, name) == 0) {
            int inode_index = root_dir[i].inode_index;

            for (int j = 0; j < MAX_OPEN_FILES; j++) {
                if (open_files[j].in_use && open_files[j].inode_index == inode_index) {
                    return -1; // File is currently open
                }
            }

            Inode *node = &inode_table[inode_index];
            node->is_allocated = false;

            for (int k = 0; k < MAX_INODE_DIRECT_OFFSETS; k++) {
                if (node->direct_blocks[k] != 0) {
                    free_block(node->direct_blocks[k]);
                    node->direct_blocks[k] = 0;
                }
            }

            if (node->indirect_block != 0) {
                char indirect_block[BLOCK_SIZE];
                block_read(node->indirect_block, indirect_block);

                for (int k = 0; k < BLOCK_ENTRIES; k++) {
                    uint16_t block_addr;
                    memcpy(&block_addr, &indirect_block[k * sizeof(uint16_t)], sizeof(uint16_t));
                    if (block_addr != 0) {
                        free_block(block_addr);
                    }
                }

                free_block(node->indirect_block);
                node->indirect_block = 0;
            }

            root_dir[i].is_allocated = false;
            memset(root_dir[i].name, '\0', MAX_FILENAME_LENGTH);
            return 0; // Success
        }
    }
    return -1; // File not found
}

int fs_read(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    FileDescriptor *fd = &open_files[fildes];
    Inode *node = &inode_table[fd->inode_index];

    size_t bytes_read = 0;
    char block_buf[BLOCK_SIZE];

    while (nbyte > 0 && fd->offset < node->size) {
        int block_idx = fd->offset / BLOCK_SIZE;
        int offset_in_block = fd->offset % BLOCK_SIZE;
        uint16_t data_block = (block_idx < MAX_INODE_DIRECT_OFFSETS) ? node->direct_blocks[block_idx] : 0;

        if (block_idx >= MAX_INODE_DIRECT_OFFSETS) {
            if (node->indirect_block == 0) {
                return bytes_read; // No more data to read
            }

            char indirect_block[BLOCK_SIZE];
            if (block_read(node->indirect_block, indirect_block) < 0) {
                return -1; // Read error
            }
            memcpy(&data_block, &indirect_block[(block_idx - MAX_INODE_DIRECT_OFFSETS) * sizeof(uint16_t)], sizeof(uint16_t));
            if (data_block == 0) {
                return bytes_read; // No more data to read
            }
        }

        if (block_read(data_block, block_buf) < 0) {
            return -1; // Read error
        }

        size_t to_copy = BLOCK_SIZE - offset_in_block;
        if (to_copy > nbyte) {
            to_copy = nbyte;
        }
        if (to_copy > node->size - fd->offset) {
            to_copy = node->size - fd->offset;
        }

        memcpy((char *)buf + bytes_read, block_buf + offset_in_block, to_copy);
        fd->offset += to_copy;
        bytes_read += to_copy;
        nbyte -= to_copy;
    }

    return bytes_read;
}

int fs_write(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    FileDescriptor *fd = &open_files[fildes];
    Inode *node = &inode_table[fd->inode_index];

    size_t bytes_written = 0;
    char block_buf[BLOCK_SIZE];

    while (nbyte > 0) {
        int block_idx = fd->offset / BLOCK_SIZE;
        int offset_in_block = fd->offset % BLOCK_SIZE;

        uint16_t *data_block = (block_idx < MAX_INODE_DIRECT_OFFSETS)
                                   ? &node->direct_blocks[block_idx]
                                   : NULL;

        if (block_idx >= MAX_INODE_DIRECT_OFFSETS) {
            // Handle indirect blocks
            char indirect_block[BLOCK_SIZE];
            if (node->indirect_block == 0) {
                node->indirect_block = allocate_block();
                memset(indirect_block, 0, BLOCK_SIZE);
                block_write(node->indirect_block, indirect_block);
            }
            block_read(node->indirect_block, indirect_block);
            data_block = (uint16_t *)&indirect_block[(block_idx - MAX_INODE_DIRECT_OFFSETS) * sizeof(uint16_t)];
        }

        if (*data_block == 0) {
            *data_block = allocate_block();
        }

        // Read the existing block content to ensure unrelated data is preserved
        if (block_read(*data_block, block_buf) < 0) {
            return -1;
        }

        // Calculate how much to write into the current block
        size_t to_copy = BLOCK_SIZE - offset_in_block;
        if (to_copy > nbyte) {
            to_copy = nbyte;
        }

        // Write only the relevant section of the block
        memcpy(block_buf + offset_in_block, (char *)buf + bytes_written, to_copy);
        if (block_write(*data_block, block_buf) < 0) {
            return -1;
        }

        // Update tracking variables
        fd->offset += to_copy;
        bytes_written += to_copy;
        nbyte -= to_copy;
    }

    // Update the file size if offset exceeds current size
    if (fd->offset > node->size) {
        node->size = fd->offset;
    }

    return bytes_written;
}


int fs_get_filesize(int fildes) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    return inode_table[open_files[fildes].inode_index].size;
}

int fs_listfiles(char ***files) {
    // Check if the output parameter is valid
    if (!files) {
        return -1; // Invalid pointer
    }

    // Allocate memory for the list of file names
    char **file_list = malloc(MAX_FILES * sizeof(char *));
    if (!file_list) {
        return -1; // Memory allocation failure
    }

    int file_count = 0;

    // Traverse the root directory to copy file names
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].is_allocated) {
            // Allocate memory for each file name
            file_list[file_count] = malloc(MAX_FILENAME_LENGTH + 1);
            if (!file_list[file_count]) {
                // Free previously allocated memory on failure
                for (int j = 0; j < file_count; j++) {
                    free(file_list[j]);
                }
                free(file_list);
                return -1;
            }

            // Use memcpy to copy the file name
            memcpy(file_list[file_count], root_dir[i].name, MAX_FILENAME_LENGTH);
            file_list[file_count][MAX_FILENAME_LENGTH] = '\0'; // Ensure null-termination
            file_count++;
        }
    }

    // Assign the allocated list to the output parameter
    *files = file_list;

    // Return success
    return 0;
}




int fs_lseek(int fildes, off_t offset) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    Inode *node = &inode_table[open_files[fildes].inode_index];
    if (offset < 0 || offset > node->size) {
        return -1; // Offset out of bounds
    }

    open_files[fildes].offset = offset;
    return 0;
}

int fs_truncate(int fildes, off_t length) {
    if (fildes < 0 || fildes >= MAX_OPEN_FILES || !open_files[fildes].in_use) {
        return -1; // Invalid file descriptor
    }

    FileDescriptor *fd = &open_files[fildes];
    Inode *node = &inode_table[fd->inode_index];

    if (length < 0 || length > node->size) {
        return -1; // Invalid length
    }

    char block_buf[BLOCK_SIZE];
    for (int i = length; i < node->size; i++) {
        int block_idx = i / BLOCK_SIZE;
        int offset_in_block = i % BLOCK_SIZE;

        uint16_t *data_block = (block_idx < MAX_INODE_DIRECT_OFFSETS) 
                               ? &node->direct_blocks[block_idx] 
                               : NULL;

        if (block_idx >= MAX_INODE_DIRECT_OFFSETS) {
            char indirect_block[BLOCK_SIZE];
            if (block_read(node->indirect_block, indirect_block) < 0) {
                return -1;
            }
            data_block = (uint16_t *)&indirect_block[(block_idx - MAX_INODE_DIRECT_OFFSETS) * sizeof(uint16_t)];
        }

        if (*data_block != 0) {
            block_read(*data_block, block_buf);
            block_buf[offset_in_block] = '\0';
            block_write(*data_block, block_buf);
        }
    }

    if (length == 0) {
        for (int i = 0; i < MAX_INODE_DIRECT_OFFSETS; i++) {
            if (node->direct_blocks[i] != 0) {
                free_block(node->direct_blocks[i]);
                node->direct_blocks[i] = 0;
            }
        }

        if (node->indirect_block != 0) {
            char indirect_block[BLOCK_SIZE];
            if (block_read(node->indirect_block, indirect_block) < 0) {
                return -1;
            }
            for (int i = 0; i < BLOCK_ENTRIES; i++) {
                uint16_t block_addr;
                memcpy(&block_addr, &indirect_block[i * sizeof(uint16_t)], sizeof(uint16_t));
                if (block_addr != 0) {
                    free_block(block_addr);
                }
            }
            free_block(node->indirect_block);
            node->indirect_block = 0;
        }
    }

    node->size = length;
    if (fd->offset > length) {
        fd->offset = length;
    }

    return 0;
}