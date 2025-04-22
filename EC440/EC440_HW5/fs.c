#include "fs.h"
#include "disk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define FS_MAX_FILENAME_LENGTH 32
#define FS_MAX_FILES 64
#define FS_MAX_FILE_DESCRIPTORS 32
#define FS_MAGIC_NUMBER 0x12345678
#define FAT_FREE 0
#define FAT_EOF 0xFFFFFFFF

typedef struct {
    uint32_t magic_number;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t fat_start_block;
    uint32_t fat_block_count;
    uint32_t dir_start_block;
    uint32_t dir_block_count;
    uint32_t data_start_block;
} Superblock;

typedef struct {
    char filename[FS_MAX_FILENAME_LENGTH];
    uint32_t filesize;
    uint32_t first_block_index;
    uint8_t in_use;
} DirectoryEntry;

typedef struct {
    uint8_t in_use;
    uint32_t dir_entry_index;
    uint32_t offset;
} FileDescriptor;

static Superblock superblock;
static uint32_t *fat_table = NULL;
static DirectoryEntry root_directory[FS_MAX_FILES];
static FileDescriptor fd_table[FS_MAX_FILE_DESCRIPTORS];
static int mounted = 0;

static int allocate_free_block();
static void free_block_chain(uint32_t start_block);
static int find_free_directory_entry();
static int find_file_in_directory(const char *filename);
static void write_metadata_to_disk();

static int allocate_free_block() {
    for (uint32_t i = superblock.data_start_block; i < superblock.total_blocks; i++) {
        if (fat_table[i] == FAT_FREE) {
            fat_table[i] = FAT_EOF; // Mark as end of file
            return i;
        }
    }
    return FAT_FREE; // No free blocks available
}

static void free_block_chain(uint32_t start_block) {
    uint32_t block = start_block;
    while (block != FAT_EOF && block != FAT_FREE) {
        uint32_t next_block = fat_table[block];
        fat_table[block] = FAT_FREE;
        block = next_block;
    }
}

static int find_free_directory_entry() {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!root_directory[i].in_use) {
            return i;
        }
    }
    return -1; // No free entry
}

static int find_file_in_directory(const char *filename) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (root_directory[i].in_use &&
            strncmp(root_directory[i].filename, filename, FS_MAX_FILENAME_LENGTH) == 0) {
            return i;
        }
    }
    return -1; // File not found
}

static void write_metadata_to_disk() {
    char block_buf[BLOCK_SIZE];

    // Write the superblock
    memset(block_buf, 0, BLOCK_SIZE);
    memcpy(block_buf, &superblock, sizeof(Superblock));
    block_write(0, block_buf);

    // Write the FAT table
    uint32_t *fat_ptr = fat_table;
    for (uint32_t i = 0; i < superblock.fat_block_count; i++) {
        memset(block_buf, 0, BLOCK_SIZE);
        memcpy(block_buf, fat_ptr, BLOCK_SIZE);
        block_write(superblock.fat_start_block + i, block_buf);
        fat_ptr += BLOCK_SIZE / sizeof(uint32_t);
    }

    // Write the root directory
    memset(block_buf, 0, BLOCK_SIZE);
    memcpy(block_buf, root_directory, sizeof(root_directory));
    block_write(superblock.dir_start_block, block_buf);
}

int make_fs(const char *disk_name) {
    if (make_disk((char *)disk_name) < 0) return -1;
    if (open_disk((char *)disk_name) < 0) return -1;

    memset(&superblock, 0, sizeof(Superblock));
    superblock.magic_number = FS_MAGIC_NUMBER;
    superblock.block_size = BLOCK_SIZE;
    superblock.total_blocks = DISK_BLOCKS;
    superblock.fat_start_block = 1;
    superblock.fat_block_count = 2;
    superblock.dir_start_block = 3;
    superblock.dir_block_count = 1;
    superblock.data_start_block = 4;

    fat_table = calloc(superblock.total_blocks, sizeof(uint32_t));
    if (!fat_table) {
        close_disk();
        return -1;
    }

    memset(root_directory, 0, sizeof(root_directory));
    for (int i = 0; i < superblock.total_blocks; i++) {
        fat_table[i] = FAT_FREE;
    }

    write_metadata_to_disk();
    free(fat_table);
    fat_table = NULL;
    close_disk();

    return 0;
}

int mount_fs(const char *disk_name) {
    if (mounted) {
        return -1; // Filesystem already mounted
    }

    if (open_disk((char *)disk_name) < 0) {
        return -1; // Failed to open disk
    }

    char block_buf[BLOCK_SIZE];

    // Read the superblock
    if (block_read(0, block_buf) < 0) {
        close_disk();
        return -1; // Failed to read superblock
    }
    memcpy(&superblock, block_buf, sizeof(Superblock));

    if (superblock.magic_number != FS_MAGIC_NUMBER) {
        close_disk();
        return -1; // Invalid filesystem
    }

    // Allocate and read the FAT table
    fat_table = malloc(superblock.total_blocks * sizeof(uint32_t));
    if (!fat_table) {
        close_disk();
        return -1; // Memory allocation failure
    }

    uint32_t *fat_ptr = fat_table;
    for (uint32_t i = 0; i < superblock.fat_block_count; i++) {
        if (block_read(superblock.fat_start_block + i, block_buf) < 0) {
            free(fat_table);
            fat_table = NULL;
            close_disk();
            return -1; // Failed to read FAT table
        }
        memcpy(fat_ptr, block_buf, BLOCK_SIZE);
        fat_ptr += BLOCK_SIZE / sizeof(uint32_t);
    }

    // Read the root directory
    if (block_read(superblock.dir_start_block, block_buf) < 0) {
        free(fat_table);
        fat_table = NULL;
        close_disk();
        return -1; // Failed to read root directory
    }
    memcpy(root_directory, block_buf, sizeof(root_directory));

    mounted = 1;
    memset(fd_table, 0, sizeof(fd_table)); // Clear file descriptors

    return 0; // Successful mount
}

int umount_fs(const char *disk_name) {
    if (!mounted) return -1;

    write_metadata_to_disk(); // Save all metadata to disk
    free(fat_table);
    fat_table = NULL;
    mounted = 0;
    close_disk();

    return 0;
}

int fs_create(const char *name) {
    if (!mounted || strlen(name) >= FS_MAX_FILENAME_LENGTH) return -1;

    if (find_file_in_directory(name) >= 0) return -1;

    int dir_idx = find_free_directory_entry();
    if (dir_idx < 0) return -1;

    DirectoryEntry *entry = &root_directory[dir_idx];
    strncpy(entry->filename, name, FS_MAX_FILENAME_LENGTH - 1);
    entry->filename[FS_MAX_FILENAME_LENGTH - 1] = '\0';  // Null-terminate
    entry->filesize = 0;
    entry->first_block_index = FAT_EOF;
    entry->in_use = 1;

    return 0;
}

int fs_open(const char *name) {
    if (!mounted) return -1;

    int dir_idx = find_file_in_directory(name);
    if (dir_idx < 0) return -1;

    for (int i = 0; i < FS_MAX_FILE_DESCRIPTORS; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].in_use = 1;
            fd_table[i].dir_entry_index = dir_idx;
            fd_table[i].offset = 0;
            return i;
        }
    }

    return -1;
}

int fs_close(int fildes) {
    if (fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use) {
        return -1;
    }
    fd_table[fildes].in_use = 0;
    return 0;
}

int fs_read(int fildes, void *buf, size_t nbyte) {
    if (!mounted || fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use) {
        return -1; // Invalid file descriptor or filesystem not mounted
    }

    FileDescriptor *fd = &fd_table[fildes];
    DirectoryEntry *entry = &root_directory[fd->dir_entry_index];
    uint32_t remaining = entry->filesize - fd->offset;
    uint32_t to_read = (nbyte > remaining) ? remaining : nbyte;
    uint32_t bytes_read = 0;

    char block_buf[BLOCK_SIZE];
    uint32_t block = entry->first_block_index;

    if (block == FAT_EOF || fd->offset >= entry->filesize) {
        return 0; // Nothing to read
    }

    uint32_t offset = fd->offset;

    // Traverse to the starting block
    while (offset >= BLOCK_SIZE) {
        block = fat_table[block];
        if (block == FAT_EOF) {
            return 0; // Offset beyond EOF
        }
        offset -= BLOCK_SIZE;
    }

    // Read data across blocks
    while (bytes_read < to_read) {
        if (block_read(block, block_buf) < 0) {
            return -1; // Read error
        }

        uint32_t chunk = BLOCK_SIZE - (offset % BLOCK_SIZE);
        if (chunk > to_read - bytes_read) {
            chunk = to_read - bytes_read;
        }

        memcpy(buf + bytes_read, block_buf + (offset % BLOCK_SIZE), chunk);

        bytes_read += chunk;
        offset += chunk;

        if (offset % BLOCK_SIZE == 0) {
            block = fat_table[block];
            if (block == FAT_EOF) {
                break; // End of file reached
            }
        }
    }

    fd->offset += bytes_read;
    return bytes_read; // Return the number of bytes read
}

int fs_write(int fildes, const void *buf, size_t nbyte) {
    if (!mounted || fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use) {
        return -1;
    }

    FileDescriptor *fd = &fd_table[fildes];
    DirectoryEntry *entry = &root_directory[fd->dir_entry_index];
    if (nbyte == 0) {
        return 0;
    }

    if (entry->first_block_index == FAT_EOF) {
        uint32_t first_block = allocate_free_block();
        if (first_block == FAT_FREE) {
            return 0;
        }
        entry->first_block_index = first_block;
        entry->filesize = 0;
    }

    if (fd->offset > entry->filesize) {
        size_t extension_needed = fd->offset - entry->filesize;
        uint32_t ext_offset = entry->filesize;
        uint32_t current_block = entry->first_block_index;
        uint32_t tmp_off = ext_offset;
        uint32_t extended = 0;

        while (tmp_off >= BLOCK_SIZE && current_block != FAT_EOF) {
            current_block = fat_table[current_block];
            tmp_off -= BLOCK_SIZE;
        }

        while (tmp_off >= BLOCK_SIZE) {
            uint32_t new_block = allocate_free_block();
            if (new_block == FAT_FREE) {
                fd->offset = entry->filesize + extended;
                if (fd->offset > entry->filesize)
                    entry->filesize = fd->offset;
                write_metadata_to_disk();
                return 0;
            }
            if (current_block == FAT_EOF) {
                entry->first_block_index = new_block;
            } else {
                fat_table[current_block] = new_block;
            }
            current_block = new_block;
            tmp_off -= BLOCK_SIZE;
        }

        if (current_block == FAT_EOF) {
            uint32_t new_block = allocate_free_block();
            if (new_block == FAT_FREE) {
                fd->offset = entry->filesize + extended;
                if (fd->offset > entry->filesize)
                    entry->filesize = fd->offset;
                write_metadata_to_disk();
                return 0;
            }
            if (entry->first_block_index == FAT_EOF) {
                entry->first_block_index = new_block;
            } else {
                uint32_t last_block = entry->first_block_index;
                while (fat_table[last_block] != FAT_EOF) {
                    last_block = fat_table[last_block];
                }
                fat_table[last_block] = new_block;
            }
            current_block = new_block;
        }

        uint32_t block = current_block;
        uint32_t offset_in_block = tmp_off;
        uint32_t remaining = (uint32_t)extension_needed;
        char block_buf[BLOCK_SIZE];

        while (remaining > 0) {
            if (block_read(block, block_buf) < 0) {
                fd->offset = entry->filesize + extended;
                if (fd->offset > entry->filesize)
                    entry->filesize = fd->offset;
                write_metadata_to_disk();
                return (int)extended;
            }
            uint32_t chunk = BLOCK_SIZE - offset_in_block;
            if (chunk > remaining) {
                chunk = remaining;
            }
            memset(block_buf + offset_in_block, 0, chunk);
            if (block_write(block, block_buf) < 0) {
                fd->offset = entry->filesize + extended;
                if (fd->offset > entry->filesize)
                    entry->filesize = fd->offset;
                write_metadata_to_disk();
                return (int)extended;
            }
            extended += chunk;
            remaining -= chunk;
            offset_in_block += chunk;

            if (offset_in_block == BLOCK_SIZE && remaining > 0) {
                uint32_t next_block = fat_table[block];
                if (next_block == FAT_EOF) {
                    next_block = allocate_free_block();
                    if (next_block == FAT_FREE) {
                        fd->offset = entry->filesize + extended;
                        if (fd->offset > entry->filesize)
                            entry->filesize = fd->offset;
                        write_metadata_to_disk();
                        return (int)extended;
                    }
                    fat_table[block] = next_block;
                }
                block = next_block;
                offset_in_block = 0;
            }
        }

        entry->filesize += extension_needed;
    }

    uint32_t file_offset = fd->offset;
    uint32_t current_block2 = entry->first_block_index;
    uint32_t temp_offset2 = file_offset;
    uint32_t bytes_written = 0;

    while (temp_offset2 >= BLOCK_SIZE && current_block2 != FAT_EOF) {
        current_block2 = fat_table[current_block2];
        temp_offset2 -= BLOCK_SIZE;
    }

    while (temp_offset2 >= BLOCK_SIZE) {
        uint32_t new_block = allocate_free_block();
        if (new_block == FAT_FREE) {
            fd->offset += bytes_written;
            if (fd->offset > entry->filesize)
                entry->filesize = fd->offset;
            write_metadata_to_disk();
            return (int)bytes_written;
        }
        if (current_block2 == FAT_EOF) {
            entry->first_block_index = new_block;
        } else {
            fat_table[current_block2] = new_block;
        }
        current_block2 = new_block;
        temp_offset2 -= BLOCK_SIZE;
    }

    if (current_block2 == FAT_EOF) {
        uint32_t new_block = allocate_free_block();
        if (new_block == FAT_FREE) {
            fd->offset += bytes_written;
            if (fd->offset > entry->filesize)
                entry->filesize = fd->offset;
            write_metadata_to_disk();
            return (int)bytes_written;
        }
        if (entry->first_block_index == FAT_EOF) {
            entry->first_block_index = new_block;
        } else {
            uint32_t last_block = entry->first_block_index;
            while (fat_table[last_block] != FAT_EOF) {
                last_block = fat_table[last_block];
            }
            fat_table[last_block] = new_block;
        }
        current_block2 = new_block;
    }

    uint32_t block2 = current_block2;
    uint32_t offset_in_block2 = temp_offset2;
    uint32_t remaining2 = (uint32_t)nbyte;
    char block_buf2[BLOCK_SIZE];

    while (remaining2 > 0) {
        if (block_read(block2, block_buf2) < 0) {
            fd->offset += bytes_written;
            if (fd->offset > entry->filesize)
                entry->filesize = fd->offset;
            write_metadata_to_disk();
            return -1;
        }
        uint32_t chunk2 = BLOCK_SIZE - offset_in_block2;
        if (chunk2 > remaining2) {
            chunk2 = remaining2;
        }
        memcpy(block_buf2 + offset_in_block2, (const char *)buf + bytes_written, chunk2);
        if (block_write(block2, block_buf2) < 0) {
            fd->offset += bytes_written;
            if (fd->offset > entry->filesize)
                entry->filesize = fd->offset;
            write_metadata_to_disk();
            return -1;
        }
        bytes_written += chunk2;
        remaining2 -= chunk2;
        offset_in_block2 += chunk2;

        if (offset_in_block2 == BLOCK_SIZE && remaining2 > 0) {
            uint32_t next_block2 = fat_table[block2];
            if (next_block2 == FAT_EOF) {
                next_block2 = allocate_free_block();
                if (next_block2 == FAT_FREE) {
                    break;
                }
                fat_table[block2] = next_block2;
            }
            block2 = next_block2;
            offset_in_block2 = 0;
        }
    }

    fd->offset += bytes_written;
    if (fd->offset > entry->filesize) {
        entry->filesize = fd->offset;
    }

    write_metadata_to_disk();
    return (int)bytes_written;
}

int fs_delete(const char *name) {
    if (!mounted || !name || strlen(name) >= FS_MAX_FILENAME_LENGTH) {
        return -1;
    }

    int dir_idx = find_file_in_directory(name);
    if (dir_idx < 0) {
        return -1; // File not found
    }

    for (int i = 0; i < FS_MAX_FILE_DESCRIPTORS; i++) {
        if (fd_table[i].in_use && fd_table[i].dir_entry_index == dir_idx) {
            return -1; // Cannot delete an open file
        }
    }

    DirectoryEntry *entry = &root_directory[dir_idx];

    // Free the block chain associated with the file
    free_block_chain(entry->first_block_index);

    // Clear the directory entry
    memset(entry, 0, sizeof(DirectoryEntry));

    write_metadata_to_disk(); // Save changes to disk

    return 0;
}

int fs_get_filesize(int fildes) {
    if (!mounted || fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use) {
        return -1;
    }

    FileDescriptor *fd = &fd_table[fildes];
    DirectoryEntry *entry = &root_directory[fd->dir_entry_index];

    return entry->filesize;
}

int fs_listfiles(char ***files) {
    if (!mounted || !files) {
        return -1; // Fail if filesystem is not mounted or `files` is NULL
    }

    // Count active files
    int file_count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (root_directory[i].in_use) {
            file_count++;
        }
    }

    // Allocate memory for the file list
    char **file_list = malloc(file_count * sizeof(char *));
    if (!file_list) {
        return -1; // Memory allocation failure
    }

    int idx = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (root_directory[i].in_use) {
            file_list[idx] = malloc(FS_MAX_FILENAME_LENGTH);
            if (!file_list[idx]) {
                // Free previously allocated memory on failure
                for (int j = 0; j < idx; j++) {
                    free(file_list[j]);
                }
                free(file_list);
                return -1;
            }
            strncpy(file_list[idx], root_directory[i].filename, FS_MAX_FILENAME_LENGTH - 1);
            file_list[idx][FS_MAX_FILENAME_LENGTH - 1] = '\0'; // Null-terminate
            idx++;
        }
    }

    *files = file_list;

    return 0; // Always return 0 as required
}

int fs_lseek(int fildes, off_t offset) {
    if (!mounted || fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use || offset < 0) {
        return -1; // Invalid parameters
    }

    FileDescriptor *fd = &fd_table[fildes];
    DirectoryEntry *entry = &root_directory[fd->dir_entry_index];

    if (offset > entry->filesize) {
        return -1; // Offset cannot exceed file size
    }

    fd->offset = offset;
    return 0;
}

int fs_truncate(int fildes, off_t length) {
    if (!mounted || fildes < 0 || fildes >= FS_MAX_FILE_DESCRIPTORS || !fd_table[fildes].in_use || length < 0) {
        return -1;
    }

    FileDescriptor *fd = &fd_table[fildes];
    DirectoryEntry *entry = &root_directory[fd->dir_entry_index];

    if ((uint32_t)length > entry->filesize) {
        return -1; // Cannot extend the file size with truncate
    }

    uint32_t block = entry->first_block_index;
    uint32_t bytes_processed = 0;

    while (block != FAT_EOF && bytes_processed + BLOCK_SIZE <= (uint32_t)length) {
        block = fat_table[block];
        bytes_processed += BLOCK_SIZE;
    }

    // Free the remaining blocks if the file is being shortened
    if (block != FAT_EOF) {
        free_block_chain(fat_table[block]);
        fat_table[block] = FAT_EOF;
    }

    entry->filesize = length;

    // Adjust the offset if it exceeds the new file size
    if (fd->offset > (uint32_t)length) {
        fd->offset = length;
    }

    write_metadata_to_disk(); // Save changes to disk

    return 0;
}
