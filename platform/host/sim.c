/* PC simulator: runs the real firmware UI against fake hardware.
 *
 *   fos_sim [command...]
 *
 * Commands are executed in order on a virtual clock:
 *   up down left right ok back     short key press
 *   long:KEY                       long press (600 ms)
 *   hold:KEY:MS                    hold a key for MS milliseconds
 *   wait:MS                        let time pass
 *   shot:NAME                      save the screen as OUT/NAME.ppm
 *   text                           print the screen to stdout as ASCII art
 *   inject:FILE.sub                play a capture into the receiver
 *   emitter:HZ:DBM                 add a carrier to the simulated spectrum
 *   external:0|1                   external radio module present
 *
 * Environment: FOS_SD = directory used as the SD card (default sim_out/sd),
 *              FOS_OUT = output directory (default sim_out).
 * Every transmitted pulse is appended to OUT/tx.log.
 */
#include "app/main.h"
#include "core/input.h"
#include "hal/hal.h"
#include "hal/radio.h"
#include "hal/storage.h"
#include "subghz/cc1101_regs.h"
#include "subghz/region.h"
#include "subghz/sub_file.h"
#include "app/settings.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* out_dir = "sim_out";
static const char* sd_dir = "sim_out/sd";

/* ------------------------------------------------------------ time */

static uint32_t now_ms = 1000;
static uint64_t now_us_extra;

uint32_t hal_millis(void) {
    return now_ms;
}
void hal_delay_ms(uint32_t ms) {
    now_ms += ms;
}
void hal_idle(void) {
    now_ms += 10;
}

/* ------------------------------------------------------------ display, keys, power */

static uint8_t screen[1024];
static uint8_t key_mask;

void hal_display_flush(const uint8_t* buf) {
    memcpy(screen, buf, sizeof(screen));
}
void hal_display_contrast(int offset) {
    (void)offset;
}
void hal_backlight(uint8_t level) {
    (void)level;
}
void hal_led(uint8_t r, uint8_t g, uint8_t b) {
    (void)r, (void)g, (void)b;
}
uint8_t hal_keys(void) {
    return key_mask;
}
bool hal_battery(HalBattery* out) {
    *out = (HalBattery){.percent = 87, .charging = false, .millivolts = 4012};
    return true;
}
void hal_ext_5v(bool on) {
    (void)on;
}
void hal_power_off(void) {
    printf("[sim] power off\n");
    exit(0);
}
void hal_reboot(void) {
    printf("[sim] reboot\n");
    exit(0);
}
void hal_reboot_dfu(void) {
    printf("[sim] reboot to DFU\n");
    exit(0);
}
void hal_log(const char* text) {
    printf("[log] %s\n", text);
}

static bool pixel(int x, int y) {
    return screen[(y >> 3) * 128 + x] >> (y & 7) & 1;
}

static void save_shot(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
    FILE* f = fopen(path, "wb");
    if(!f) {
        perror(path);
        return;
    }
    const int scale = 4;
    fprintf(f, "P6\n%d %d\n255\n", 128 * scale, 64 * scale);
    for(int y = 0; y < 64 * scale; y++)
        for(int x = 0; x < 128 * scale; x++) {
            bool on = pixel(x / scale, y / scale);
            fputc(on ? 20 : 255, f);
            fputc(on ? 12 : 140, f);
            fputc(on ? 0 : 40, f);
        }
    fclose(f);
}

static void print_text(void) {
    for(int y = 0; y < 64; y += 2) {
        for(int x = 0; x < 128; x++) {
            bool a = pixel(x, y), b = pixel(x, y + 1);
            fputs(a && b ? "█" : a ? "▀" : b ? "▄" : " ", stdout);
        }
        fputc('\n', stdout);
    }
}

/* ------------------------------------------------------------ storage */

struct HalFile {
    FILE* f;
};
static struct HalFile files[4];

static void host_path(char* out, size_t size, const char* path) {
    snprintf(out, size, "%s%s", sd_dir, path);
}

bool hal_storage_ready(void) {
    struct stat st;
    return stat(sd_dir, &st) == 0 && S_ISDIR(st.st_mode);
}
HalFile* hal_file_open(const char* path, const char* mode) {
    char p[600];
    host_path(p, sizeof(p), path);
    for(size_t i = 0; i < 4; i++) {
        if(files[i].f) continue;
        files[i].f = fopen(p, mode[0] == 'w' ? "wb" : "rb");
        return files[i].f ? &files[i] : NULL;
    }
    return NULL;
}
int hal_file_read(HalFile* f, void* buf, size_t size) {
    return (int)fread(buf, 1, size, f->f);
}
int hal_file_write(HalFile* f, const void* buf, size_t size) {
    return (int)fwrite(buf, 1, size, f->f);
}
void hal_file_close(HalFile* f) {
    if(f && f->f) fclose(f->f);
    if(f) f->f = NULL;
}
bool hal_file_exists(const char* path) {
    char p[600];
    struct stat st;
    host_path(p, sizeof(p), path);
    return stat(p, &st) == 0;
}
bool hal_file_remove(const char* path) {
    char p[600];
    host_path(p, sizeof(p), path);
    return remove(p) == 0;
}
bool hal_mkdir(const char* path) {
    char p[600];
    host_path(p, sizeof(p), path);
    return mkdir(p, 0777) == 0 || hal_file_exists(path);
}

static int name_cmp(const void* a, const void* b) {
    return strcmp(((const HalDirEntry*)a)->name, ((const HalDirEntry*)b)->name);
}

int hal_dir_list(const char* path, HalDirEntry* out, int max, int skip) {
    char p[600];
    host_path(p, sizeof(p), path);
    DIR* d = opendir(p);
    if(!d) return -1;
    static HalDirEntry all[256];
    int n = 0;
    struct dirent* e;
    while((e = readdir(d)) && n < 256) {
        if(e->d_name[0] == '.') continue;
        char full[900];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", p, e->d_name);
        if(stat(full, &st) != 0) continue;
        size_t len = strlen(e->d_name);
        if(len >= sizeof(all[n].name)) continue; /* the firmware only shows short names */
        memcpy(all[n].name, e->d_name, len + 1);
        all[n].is_dir = S_ISDIR(st.st_mode);
        all[n].size = (uint32_t)st.st_size;
        n++;
    }
    closedir(d);
    qsort(all, (size_t)n, sizeof(all[0]), name_cmp); /* stable order for screenshots */
    int count = 0;
    for(int i = skip; i < n && count < max; i++) out[count++] = all[i];
    return count;
}

/* ------------------------------------------------------------ radio */

static bool external_present;
static RadioId current = RADIO_INTERNAL;
static uint32_t rx_bw = 650000, tuned = 433920000;
static const char* preset_name = "?";

static struct {
    uint32_t hz;
    int dbm;
} emitters[8];
static int n_emitters;

#define INJECT_MAX 400000
static sg_pulse_t inject[INJECT_MAX];
static size_t inject_n, inject_pos;
static uint64_t inject_clock_us; /* time already delivered */
static uint32_t capture_started_ms, last_edge_ms;
static bool capturing;

static FILE* tx_log;
static bool tx_on, tx_finishing;
static size_t tx_queued;

bool radio_present(RadioId id) {
    return id == RADIO_INTERNAL || external_present;
}
bool radio_select(RadioId id) {
    if(!radio_present(id)) return false;
    current = id;
    return true;
}
RadioId radio_current(void) {
    return current;
}
void radio_configure(const SgPreset* p) {
    preset_name = p->sub_name;
    for(const uint8_t* r = p->regs; r[0] || r[1]; r += 2)
        if(r[0] == CC1101_MDMCFG4) {
            unsigned e = r[1] >> 6, m = (r[1] >> 4) & 3;
            rx_bw = CC1101_XTAL_HZ / (8 * (4 + m) * (1u << e));
        }
}
uint32_t radio_set_frequency(uint32_t hz) {
    tuned = hz;
    return hz;
}
void radio_rx(void) {
}
void radio_idle(void) {
}
void radio_sleep(void) {
}

static int rssi_at(uint32_t hz) {
    int best = -99 + rand() % 4;
    for(int i = 0; i < n_emitters; i++) {
        uint32_t df = hz > emitters[i].hz ? hz - emitters[i].hz : emitters[i].hz - hz;
        int v = emitters[i].dbm;
        if(df > rx_bw / 2) v -= (int)((uint64_t)(df - rx_bw / 2) * 24 / rx_bw) + 3; /* filter skirt */
        v += rand() % 3 - 1;
        if(v > best) best = v;
    }
    /* a capture being played back looks like a strong signal */
    if(capturing && inject_pos < inject_n && best < -52) best = -52 + rand() % 3;
    return best;
}

int radio_rssi(void) {
    return rssi_at(tuned);
}
int radio_measure(uint32_t hz) {
    tuned = hz;
    now_us_extra += 1500;
    return rssi_at(hz);
}

void radio_capture_start(void) {
    capturing = true;
    capture_started_ms = now_ms;
    last_edge_ms = now_ms;
    inject_clock_us = 0;
}
void radio_capture_stop(void) {
    capturing = false;
}
size_t radio_capture_read(sg_pulse_t* out, size_t max) {
    if(!capturing) return 0;
    uint64_t elapsed = (uint64_t)(now_ms - capture_started_ms) * 1000;
    size_t n = 0;
    while(n < max && inject_pos < inject_n) {
        sg_pulse_t p = inject[inject_pos];
        uint64_t dur = (uint64_t)(p < 0 ? -p : p);
        if(inject_clock_us + dur > elapsed) break;
        inject_clock_us += dur;
        out[n++] = p;
        inject_pos++;
        last_edge_ms = now_ms;
    }
    return n;
}
bool radio_capture_overflowed(void) {
    return false;
}
uint32_t radio_capture_quiet_us(void) {
    return (now_ms - last_edge_ms) * 1000;
}

bool radio_tx_start(void) {
    /* the firmware must never get here for a blocked frequency */
    if(!sg_region_tx_allowed((SgRegion)settings.region, tuned)) {
        fprintf(stderr, "[sim] BUG: transmission on a blocked frequency %u\n", (unsigned)tuned);
        abort();
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/tx.log", out_dir);
    tx_log = fopen(path, "a");
    if(!tx_log) return false;
    fprintf(tx_log, "TX %u %s %s\n", (unsigned)tuned, preset_name, current == RADIO_EXTERNAL ? "EXT" : "INT");
    tx_on = true;
    tx_finishing = false;
    tx_queued = 0;
    return true;
}
bool radio_tx_push(sg_pulse_t p) {
    if(!tx_on || tx_queued >= 4096) return false;
    fprintf(tx_log, "%d\n", (int)p);
    tx_queued++;
    return true;
}
size_t radio_tx_space(void) {
    return 4096 - tx_queued;
}
void radio_tx_finish(void) {
    tx_finishing = true;
}
bool radio_tx_busy(void) {
    /* drain the queue a little on every poll */
    if(tx_on && tx_queued) tx_queued = tx_queued > 512 ? tx_queued - 512 : 0;
    return tx_on && (tx_queued || !tx_finishing);
}
void radio_tx_stop(void) {
    if(tx_log) {
        fprintf(tx_log, "END\n");
        fclose(tx_log);
    }
    tx_log = NULL;
    tx_on = false;
}

static void inject_pulse(sg_pulse_t p, void* ctx) {
    (void)ctx;
    if(inject_n < INJECT_MAX) inject[inject_n++] = p;
}

static void load_inject(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        perror(path);
        exit(2);
    }
    static SubParser parser;
    sub_parser_init(&parser, inject_pulse, NULL);
    inject_n = inject_pos = 0;
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0) sub_parser_feed(&parser, buf, n);
    fclose(f);
    sub_parser_finish(&parser);
    /* restart the playback clock from "now" */
    capture_started_ms = now_ms;
    inject_clock_us = 0;
}

/* ------------------------------------------------------------ script */

static void run_ms(uint32_t ms) {
    uint32_t end = now_ms + ms;
    while((int32_t)(end - now_ms) > 0) fos_step();
}

static int key_by_name(const char* s) {
    static const char* const names[KEY_COUNT] = {"up", "down", "left", "right", "ok", "back"};
    for(int i = 0; i < KEY_COUNT; i++)
        if(strcmp(s, names[i]) == 0) return i;
    return -1;
}

static void press(int key, uint32_t ms) {
    key_mask = (uint8_t)(1u << key);
    run_ms(ms);
    key_mask = 0;
    run_ms(80);
}

int main(int argc, char** argv) {
    if(getenv("FOS_OUT")) out_dir = getenv("FOS_OUT");
    if(getenv("FOS_SD")) sd_dir = getenv("FOS_SD");
    mkdir(out_dir, 0777);
    srand(1);
    /* commands that describe the hardware must act before the firmware boots */
    for(int i = 1; i < argc; i++)
        if(strncmp(argv[i], "external:", 9) == 0) external_present = atoi(argv[i] + 9) != 0;
    fos_start();
    run_ms(100);
    for(int i = 1; i < argc; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s", argv[i]);
        char* arg = strchr(cmd, ':');
        if(arg) *arg++ = '\0';
        int key = key_by_name(cmd);
        if(key >= 0) {
            press(key, 80);
        } else if(strcmp(cmd, "long") == 0 && arg && key_by_name(arg) >= 0) {
            press(key_by_name(arg), 600);
        } else if(strcmp(cmd, "hold") == 0 && arg) {
            char* ms = strchr(arg, ':');
            if(ms) *ms++ = '\0';
            if(key_by_name(arg) >= 0) press(key_by_name(arg), ms ? (uint32_t)atoi(ms) : 1000);
        } else if(strcmp(cmd, "wait") == 0 && arg) {
            run_ms((uint32_t)atoi(arg));
        } else if(strcmp(cmd, "shot") == 0 && arg) {
            run_ms(50);
            save_shot(arg);
        } else if(strcmp(cmd, "text") == 0) {
            run_ms(50);
            print_text();
        } else if(strcmp(cmd, "inject") == 0 && arg) {
            load_inject(arg);
        } else if(strcmp(cmd, "emitter") == 0 && arg && n_emitters < 8) {
            char* dbm = strchr(arg, ':');
            if(dbm) *dbm++ = '\0';
            emitters[n_emitters].hz = (uint32_t)strtoul(arg, NULL, 10);
            emitters[n_emitters].dbm = dbm ? atoi(dbm) : -50;
            n_emitters++;
        } else if(strcmp(cmd, "external") == 0) {
            /* handled before boot */
        } else {
            fprintf(stderr, "unknown command: %s\n", argv[i]);
            return 2;
        }
    }
    return 0;
}
