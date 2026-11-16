#include "cc1101.h"
#include "../subghz/presets.h"

/* Every access starts by pulling CS low and waiting for the chip to signal
 * that its crystal is running (MISO low). */
static bool begin(const Cc1101Bus* bus) {
    bus->select(true);
    return bus->wait_ready();
}

uint8_t cc1101_strobe(const Cc1101Bus* bus, uint8_t strobe) {
    uint8_t status = 0xFF;
    if(begin(bus)) status = bus->xfer(strobe);
    bus->select(false);
    return status;
}

void cc1101_write(const Cc1101Bus* bus, uint8_t reg, uint8_t value) {
    if(begin(bus)) {
        bus->xfer(reg);
        bus->xfer(value);
    }
    bus->select(false);
}

uint8_t cc1101_read(const Cc1101Bus* bus, uint8_t reg) {
    uint8_t value = 0xFF;
    if(begin(bus)) {
        bus->xfer(reg | CC1101_READ);
        value = bus->xfer(0);
    }
    bus->select(false);
    return value;
}

/* Status registers share addresses with the strobes; the burst bit selects them. */
uint8_t cc1101_status(const Cc1101Bus* bus, uint8_t reg) {
    return cc1101_read(bus, reg | CC1101_BURST);
}

bool cc1101_probe(const Cc1101Bus* bus) {
    uint8_t version = cc1101_status(bus, CC1101_VERSION);
    uint8_t part = cc1101_status(bus, CC1101_PARTNUM);
    /* genuine chips report part 0x00 and version 0x04, 0x14 or similar; a
     * missing chip reads as all ones or all zeroes */
    return part == 0x00 && version != 0x00 && version != 0xFF;
}

bool cc1101_reset(const Cc1101Bus* bus) {
    /* manual reset sequence from the datasheet */
    bus->select(false);
    bus->delay_us(10);
    bus->select(true);
    bus->delay_us(10);
    bus->select(false);
    bus->delay_us(45);
    bool ok = begin(bus);
    if(ok) {
        bus->xfer(CC1101_SRES);
        ok = bus->wait_ready(); /* MISO goes low again when the reset is done */
    }
    bus->select(false);
    return ok;
}

void cc1101_load(const Cc1101Bus* bus, const uint8_t* regs, const uint8_t patable[8]) {
    for(; regs[0] || regs[1]; regs += 2) cc1101_write(bus, regs[0], regs[1]);
    if(begin(bus)) {
        bus->xfer(CC1101_PATABLE | CC1101_BURST);
        for(int i = 0; i < 8; i++) bus->xfer(patable[i]);
    }
    bus->select(false);
}

uint32_t cc1101_set_frequency(const Cc1101Bus* bus, uint32_t hz) {
    uint32_t word = sg_cc1101_freq_word(hz);
    cc1101_write(bus, CC1101_FREQ2, (uint8_t)(word >> 16));
    cc1101_write(bus, CC1101_FREQ1, (uint8_t)(word >> 8));
    cc1101_write(bus, CC1101_FREQ0, (uint8_t)word);
    return sg_cc1101_freq_hz(word);
}

bool cc1101_wait_state(const Cc1101Bus* bus, uint8_t state, uint32_t timeout_us) {
    for(uint32_t waited = 0; waited <= timeout_us; waited += 50) {
        if((cc1101_status(bus, CC1101_MARCSTATE) & 0x1F) == state) return true;
        bus->delay_us(50);
    }
    return false;
}

bool cc1101_idle(const Cc1101Bus* bus) {
    cc1101_strobe(bus, CC1101_SIDLE);
    return cc1101_wait_state(bus, CC1101_STATE_IDLE, 5000);
}

bool cc1101_rx(const Cc1101Bus* bus) {
    cc1101_strobe(bus, CC1101_SRX);
    return cc1101_wait_state(bus, CC1101_STATE_RX, 10000);
}

bool cc1101_tx(const Cc1101Bus* bus) {
    cc1101_strobe(bus, CC1101_STX);
    return cc1101_wait_state(bus, CC1101_STATE_TX, 10000);
}

void cc1101_sleep(const Cc1101Bus* bus) {
    cc1101_strobe(bus, CC1101_SIDLE);
    cc1101_strobe(bus, CC1101_SPWD);
}

int cc1101_rssi_dbm(const Cc1101Bus* bus) {
    int raw = cc1101_status(bus, CC1101_RSSI);
    if(raw >= 128) raw -= 256;
    return raw / 2 - 74;
}
