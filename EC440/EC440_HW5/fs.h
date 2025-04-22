#ifndef INCLUDE_FS_H
#define INCLUDE_FS_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/* File system parameters */
#define FS_MAX_FILES 64
#define FS_MAX_FILENAME_LENGTH 32
#define FS_MAX_FILE_DESCRIPTORS 32
#define BLOCK_SIZE 4096
#define FS_MAGIC_NUMBER 0x12345678
#define FAT_FREE 0
#define FAT_EOF 0xFFFFFFFF

/* Function prototypes */
int make_fs(const char *disk_name);        // Create a new file system on a virtual disk
int mount_fs(const char *disk_name);       // Mount an existing virtual disk to the file system
int umount_fs(const char *disk_name);      // Unmount the currently mounted virtual disk

int fs_open(const char *name);             // Open an existing file and return a file descriptor
int fs_close(int fildes);                  // Close an open file descriptor

int fs_create(const char *name);           // Create a new file with a given name
int fs_delete(const char *name);           // Delete an existing file by its name

int fs_read(int fildes, void *buf, size_t nbyte);  // Read data from an open file
int fs_write(int fildes, const void *buf, size_t nbyte); // Write data to an open file

int fs_get_filesize(int fildes);           // Get the size of an open file
int fs_listfiles(char ***files);           // List all files in the directory

int fs_lseek(int fildes, off_t offset);    // Move the file offset of an open file
int fs_truncate(int fildes, off_t length); // Truncate an open file to a specified length

#endif /* INCLUDE_FS_H */
