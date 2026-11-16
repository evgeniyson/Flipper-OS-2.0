#pragma once
/* CC1101 transceiver driver. Talks to the chip through a small bus interface
 * so the same code serves the on-board chip, an external module and the tests. */
#include "../subghz/cc1101_regs.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void (*select)(bool active); /* drive chip-select */
    bool (*wait_ready)(void); /* wait for MISO to go low; false on timeout */
    uint8_t (*xfer)(uint8_t byte); /* exchange one byte */
    void (*delay_us)(uint32_t us);
} Cc1101Bus;

enum { CC1101_STATE_IDLE = 0x01, CC1101_STATE_RX = 0x0D, CC1101_STATE_TX = 0x13 };

uint8_t cc1101_strobe(const Cc1101Bus* bus, uint8_t strobe);
void cc1101_write(const Cc1101Bus* bus, uint8_t reg, uint8_t value);
uint8_t cc1101_read(const Cc1101Bus* bus, uint8_t reg);
uint8_t cc1101_status(const Cc1101Bus* bus, uint8_t reg);

/* True when a chip answers with a plausible version number. */
bool cc1101_probe(const Cc1101Bus* bus);
bool cc1101_reset(const Cc1101Bus* bus);
/* regs: {register, value} pairs terminated by {0, 0}; then the power table. */
void cc1101_load(const Cc1101Bus* bus, const uint8_t* regs, const uint8_t patable[8]);
uint32_t cc1101_set_frequency(const Cc1101Bus* bus, uint32_t hz);
bool cc1101_wait_state(const Cc1101Bus* bus, uint8_t state, uint32_t timeout_us);
bool cc1101_idle(const Cc1101Bus* bus);
bool cc1101_rx(const Cc1101Bus* bus);
bool cc1101_tx(const Cc1101Bus* bus);
void cc1101_sleep(const Cc1101Bus* bus);
int cc1101_rssi_dbm(const Cc1101Bus* bus);
