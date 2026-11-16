/* hal/storage.h on top of FatFs. */
#include "board.h"
#include "ff.h"
#include "../../hal/storage.h"
#include <string.h>

void sd_forget(void);

struct HalFile {
    FIL fil;
    bool used;
};

static FATFS fs;
static bool mounted;
static uint32_t retry_at;
static struct HalFile files[3];

void storage_init(void) {
    /* SD card supply switch: released (pulled high on the board) = on */
    pin_write(PIN_SD_POWER, true);
    pin_init(PIN_SD_POWER, PIN_OUTPUT, PULL_NONE, true, 0);
    pin_write(PIN_SD_CS, true);
    pin_init(PIN_SD_CS, PIN_OUTPUT, PULL_NONE, false, 0);
    pin_init(PIN_SD_DETECT, PIN_INPUT, PULL_UP, false, 0);
}

/* A failed transfer usually means the card was pulled out. */
static void check(FRESULT r) {
    if(r == FR_DISK_ERR || r == FR_NOT_READY || r == FR_INT_ERR) {
        mounted = false;
        sd_forget();
        for(size_t i = 0; i < 3; i++) files[i].used = false;
    }
}

bool hal_storage_ready(void) {
    if(mounted) return true;
    /* no card: do not hammer the bus, try again in a while */
    if(retry_at && (int32_t)(millis() - retry_at) < 0) return false;
    retry_at = (millis() + 3000) | 1;
    mounted = f_mount(&fs, "", 1) == FR_OK;
    return mounted;
}

HalFile* hal_file_open(const char* path, const char* mode) {
    if(!hal_storage_ready()) return NULL;
    for(size_t i = 0; i < 3; i++) {
        if(files[i].used) continue;
        BYTE flags = mode[0] == 'w' ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ;
        FRESULT r = f_open(&files[i].fil, path, flags);
        check(r);
        if(r != FR_OK) return NULL;
        files[i].used = true;
        return &files[i];
    }
    return NULL;
}

int hal_file_read(HalFile* f, void* buf, size_t size) {
    UINT done = 0;
    if(!f || !f->used) return -1;
    FRESULT r = f_read(&f->fil, buf, (UINT)size, &done);
    check(r);
    return r == FR_OK ? (int)done : -1;
}

int hal_file_write(HalFile* f, const void* buf, size_t size) {
    UINT done = 0;
    if(!f || !f->used) return -1;
    FRESULT r = f_write(&f->fil, buf, (UINT)size, &done);
    check(r);
    return r == FR_OK ? (int)done : -1;
}

void hal_file_close(HalFile* f) {
    if(!f || !f->used) return;
    check(f_close(&f->fil));
    f->used = false;
}

bool hal_file_exists(const char* path) {
    FILINFO info;
    if(!hal_storage_ready()) return false;
    FRESULT r = f_stat(path, &info);
    check(r);
    return r == FR_OK;
}

bool hal_file_remove(const char* path) {
    if(!hal_storage_ready()) return false;
    FRESULT r = f_unlink(path);
    check(r);
    return r == FR_OK;
}

bool hal_mkdir(const char* path) {
    if(!hal_storage_ready()) return false;
    FRESULT r = f_mkdir(path);
    check(r);
    return r == FR_OK || r == FR_EXIST;
}

int hal_dir_list(const char* path, HalDirEntry* out, int max, int skip) {
    static DIR dir;
    static FILINFO info;
    if(!hal_storage_ready()) return -1;
    FRESULT r = f_opendir(&dir, path);
    check(r);
    if(r != FR_OK) return -1;
    int count = 0;
    while(count < max) {
        r = f_readdir(&dir, &info);
        if(r != FR_OK || info.fname[0] == '\0') break;
        if(info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS))) continue;
        size_t len = strlen(info.fname);
        if(len >= sizeof(out[0].name)) continue; /* too long to show or reopen */
        if(skip > 0) {
            skip--;
            continue;
        }
        memcpy(out[count].name, info.fname, len + 1);
        out[count].is_dir = info.fattrib & AM_DIR;
        out[count].size = (uint32_t)info.fsize;
        count++;
    }
    check(r);
    f_closedir(&dir);
    return count;
}
