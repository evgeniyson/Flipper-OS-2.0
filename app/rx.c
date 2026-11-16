#include "rx.h"
#include "settings.h"

static SgPacketizer packetizer;
static RxPulseTap pulse_tap;
static void* tap_ctx;
static bool running;
static uint32_t frequency, overflows;

const SgPreset* rx_default_preset(void) {
    return &sg_presets[settings.preset < sg_preset_count ? settings.preset : 0];
}

void rx_start(uint32_t hz, const SgPreset* preset, SgPacketCb on_packet, RxPulseTap tap, void* ctx) {
    if(running) rx_stop();
    sg_packetizer_init(&packetizer, 0, on_packet, ctx);
    pulse_tap = tap;
    tap_ctx = ctx;
    overflows = 0;
    radio_configure(preset);
    frequency = radio_set_frequency(hz);
    radio_rx();
    radio_capture_start();
    running = true;
}

void rx_retune(uint32_t hz) {
    if(!running) return;
    radio_capture_stop();
    sg_packetizer_flush(&packetizer);
    radio_idle();
    frequency = radio_set_frequency(hz);
    radio_rx();
    radio_capture_start();
}

void rx_poll(void) {
    if(!running) return;
    sg_pulse_t buf[128];
    /* bounded so a noisy channel cannot starve the user interface */
    for(int round = 0; round < 16; round++) {
        size_t n = radio_capture_read(buf, 128);
        for(size_t i = 0; i < n; i++) {
            if(pulse_tap) pulse_tap(buf[i], tap_ctx);
            sg_packetizer_feed(&packetizer, buf[i]);
        }
        if(n < 128) break;
    }
    if(radio_capture_overflowed()) overflows++;
    /* the air went quiet: whatever is buffered is a complete packet */
    if(packetizer.in_pkt && radio_capture_quiet_us() > 2 * SG_PKT_GAP_US)
        sg_packetizer_flush(&packetizer);
}

void rx_stop(void) {
    if(!running) return;
    radio_capture_stop();
    sg_packetizer_flush(&packetizer);
    radio_idle();
    radio_sleep();
    running = false;
}

bool rx_running(void) {
    return running;
}

uint32_t rx_frequency(void) {
    return frequency;
}

uint32_t rx_overflows(void) {
    return overflows;
}
