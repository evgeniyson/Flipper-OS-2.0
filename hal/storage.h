#pragma once
/* Files on the SD card. Paths are absolute with '/' separators. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct HalFile HalFile;

bool hal_storage_ready(void); /* card present and mounted (mounts on demand) */
HalFile* hal_file_open(const char* path, const char* mode); /* "r" or "w" */
int hal_file_read(HalFile* f, void* buf, size_t size);
int hal_file_write(HalFile* f, const void* buf, size_t size);
void hal_file_close(HalFile* f);
bool hal_file_exists(const char* path);
bool hal_file_remove(const char* path);
bool hal_mkdir(const char* path);

typedef struct {
    char name[40];
    bool is_dir;
    uint32_t size;
} HalDirEntry;
/* List up to `max` entries after skipping `skip`. Returns the count, -1 on error. */
int hal_dir_list(const char* path, HalDirEntry* out, int max, int skip);
