#pragma once
/* Receive pipeline: radio capture -> packetizer -> caller. */
#include "../hal/radio.h"

typedef void (*RxPulseTap)(sg_pulse_t pulse, void* ctx);

void rx_start(uint32_t hz, const SgPreset* preset, SgPacketCb on_packet, RxPulseTap tap, void* ctx);
void rx_retune(uint32_t hz);
void rx_poll(void); /* call from the screen's tick */
void rx_stop(void);
bool rx_running(void);
uint32_t rx_frequency(void);
uint32_t rx_overflows(void);
const SgPreset* rx_default_preset(void); /* the one chosen in the settings */
