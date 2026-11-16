#pragma once
/* Sub-GHz transceiver. Two devices can exist: the CC1101 on the main board
 * and an external CC1101 module on the GPIO header. Exactly one is selected
 * at a time; every call below addresses the selected one. */
#include "../subghz/packet.h"
#include "../subghz/presets.h"

typedef enum { RADIO_INTERNAL, RADIO_EXTERNAL, RADIO_COUNT } RadioId;

/* Probe for a chip (reads its part number and version). */
bool radio_present(RadioId id);
bool radio_select(RadioId id);
RadioId radio_current(void);

/* Reset the chip and load a configuration; the chip is left idle. */
void radio_configure(const SgPreset* preset);
/* Tune (and switch the antenna path). Returns the frequency actually set. */
uint32_t radio_set_frequency(uint32_t hz);
void radio_rx(void);
void radio_idle(void);
void radio_sleep(void);
int radio_rssi(void); /* dBm, while receiving */
/* Tune, settle and measure: one point of a spectrum sweep. */
int radio_measure(uint32_t hz);

/* Edge timing of the demodulated signal. Pulses queue up in the background
 * and are drained with radio_capture_read(). */
void radio_capture_start(void);
void radio_capture_stop(void);
size_t radio_capture_read(sg_pulse_t* out, size_t max);
bool radio_capture_overflowed(void); /* reads and clears the flag */
uint32_t radio_capture_quiet_us(void); /* time since the last edge */

/* Transmission: pulses are queued and keyed out in the background.
 * Application code must not call radio_tx_start() directly: go through
 * tx_begin() in app/tx.h, which enforces the region rules. */
bool radio_tx_start(void);
bool radio_tx_push(sg_pulse_t pulse); /* false when the queue is full */
size_t radio_tx_space(void); /* pulses that can still be queued */
void radio_tx_finish(void); /* no more pulses will follow */
bool radio_tx_busy(void);
void radio_tx_stop(void);
