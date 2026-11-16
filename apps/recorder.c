/* RAW recorder: writes the pulse stream to a .sub file on the SD card.
 * With a trigger level set, only activity above that level is stored (plus a
 * short run-up, so the start of a transmission is not lost). */
#include "apps.h"

#define PRE_TRIGGER 256
#define GATE_HOLD_MS 300

static bool recording;
static HalFile* file;
static SubWriter writer;
static char file_name[20];
static char wbuf[512];
static size_t wbuf_len;
static bool write_failed;
static int rssi = -128;
static int threshold = -128; /* -128: record everything */
static uint32_t rssi_at, gate_until;
static bool gate_open;
static sg_pulse_t pre[PRE_TRIGGER];
static uint16_t pre_pos, pre_count;

static void flush_buffer(void) {
    if(wbuf_len && file && hal_file_write(file, wbuf, wbuf_len) != (int)wbuf_len) write_failed = true;
    wbuf_len = 0;
}

/* SubWriter emits tiny pieces; collect them into sector-sized writes */
static int buffered_write(void* ctx, const char* data, size_t len) {
    UNUSED(ctx);
    for(size_t i = 0; i < len; i++) {
        wbuf[wbuf_len++] = data[i];
        if(wbuf_len == sizeof(wbuf)) flush_buffer();
    }
    return write_failed ? -1 : (int)len;
}

static void on_pulse(sg_pulse_t p, void* ctx) {
    UNUSED(ctx);
    if(!recording) return;
    if(threshold == -128 || gate_open) {
        sub_writer_pulse(&writer, p);
    } else {
        pre[pre_pos] = p;
        pre_pos = (uint16_t)((pre_pos + 1) % PRE_TRIGGER);
        if(pre_count < PRE_TRIGGER) pre_count++;
    }
}

static void open_gate(void) {
    gate_open = true;
    /* separate from whatever was recorded before, then replay the run-up */
    sub_writer_pulse(&writer, -30000);
    uint16_t start = (uint16_t)((pre_pos + PRE_TRIGGER - pre_count) % PRE_TRIGGER);
    for(uint16_t i = 0; i < pre_count; i++) sub_writer_pulse(&writer, pre[(start + i) % PRE_TRIGGER]);
    pre_count = 0;
}

static bool start_recording(void) {
    if(!hal_storage_ready()) {
        ui_toast("No SD card");
        return false;
    }
    hal_mkdir(SUBGHZ_DIR);
    char path[40];
    for(int i = 1; i < 10000; i++) {
        fmt_snprintf(file_name, sizeof(file_name), "RAW_%04d.sub", i);
        fmt_snprintf(path, sizeof(path), SUBGHZ_DIR "/%s", file_name);
        if(!hal_file_exists(path)) break;
    }
    file = hal_file_open(path, "w");
    if(!file) {
        ui_toast("Cannot create file");
        return false;
    }
    wbuf_len = 0;
    write_failed = false;
    gate_open = false;
    pre_pos = pre_count = 0;
    sub_writer_begin_raw(&writer, buffered_write, NULL, rx_frequency(), rx_default_preset()->sub_name);
    recording = true;
    hal_led(60, 0, 0);
    return true;
}

static void stop_recording(void) {
    if(!recording) return;
    recording = false;
    sub_writer_end(&writer);
    flush_buffer();
    hal_file_close(file);
    file = NULL;
    hal_led(0, 0, 0);
    ui_toast(write_failed ? "Write error" : "Saved");
}

static void enter(void) {
    rx_start(settings.frequency, rx_default_preset(), NULL, on_pulse, NULL);
}

static void leave(void) {
    stop_recording();
    rx_stop();
}

static void tick(uint32_t now) {
    if(!rx_running()) enter();
    if(now - rssi_at >= 30) {
        rssi_at = now;
        rssi = radio_rssi();
        if(recording && threshold != -128) {
            if(rssi >= threshold) {
                gate_until = now + GATE_HOLD_MS;
                if(!gate_open) open_gate();
            } else if(gate_open && (int32_t)(now - gate_until) >= 0) {
                gate_open = false;
            }
        }
        ui_dirty();
    }
    rx_poll();
    if(recording && write_failed) stop_recording();
}

static void draw(void) {
    char line[40];
    radio_summary(line, sizeof(line), rx_frequency());
    cv_text(2, 11, &font_normal, line);

    int lvl = CLAMP(rssi + 100, 0, 70);
    ui_bar(2, 22, 96, 7, lvl, 70);
    if(threshold != -128) cv_vline(2 + 94 * CLAMP(threshold + 100, 0, 70) / 70, 20, 11);
    fmt_snprintf(line, sizeof(line), "%ddBm", rssi);
    cv_text_right(CV_W - 1, 23, &font_small, line);

    if(recording) {
        cv_fill(2, 34, 7, 7, true);
        fmt_snprintf(line, sizeof(line), "REC %lu pulses", (unsigned long)writer.total);
        cv_text(13, 34, &font_normal, line);
        cv_text(2, 44, &font_small, file_name);
        if(threshold != -128) cv_text_right(CV_W - 1, 44, &font_small, gate_open ? "signal" : "waiting");
    } else {
        cv_text(2, 34, &font_normal, "OK: start recording");
        if(threshold == -128)
            cv_text(2, 44, &font_small, "Trigger: off (records all)");
        else {
            fmt_snprintf(line, sizeof(line), "Trigger: above %ddBm", threshold);
            cv_text(2, 44, &font_small, line);
        }
    }
    cv_text(2, 58, &font_small, recording ? "OK: stop" : "UP/DOWN: trigger  LEFT: radio");
}

static bool input(InputEvent ev) {
    if(ev.key == KEY_OK && ev.type == INPUT_SHORT) {
        if(recording)
            stop_recording();
        else
            start_recording();
        return true;
    }
    if(recording) return true; /* nothing else while recording, not even BACK */
    if(ev.type == INPUT_PRESS && ev.key == KEY_UP)
        threshold = threshold == -128 ? -90 : MIN(threshold + 5, -40);
    if(ev.type == INPUT_PRESS && ev.key == KEY_DOWN) threshold = threshold <= -90 ? -128 : threshold - 5;
    if(ev.type == INPUT_SHORT && ev.key == KEY_LEFT) {
        rx_stop();
        ui_push(&radio_config_screen);
        return true;
    }
    return ev.key != KEY_BACK;
}

const Screen recorder_screen = {
    .title = "RAW recorder", .enter = enter, .leave = leave, .input = input, .tick = tick, .draw = draw};
