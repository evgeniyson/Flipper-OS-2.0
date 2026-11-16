/* CC1101 driver against a register-level model of the chip's SPI protocol. */
#include "test.h"
#include "drivers/cc1101.h"
#include "subghz/presets.h"

static uint8_t regs[0x30], status_regs[0x10], patable[8];
static uint8_t marcstate = CC1101_STATE_IDLE;
static bool selected, powered_down, present = true;
static int phase; /* bytes since CS went low */
static uint8_t header;
static int strobes[0x10];

static void m_select(bool active) {
    if(active && !selected) phase = 0;
    if(active) powered_down = false; /* CS wakes the chip */
    selected = active;
}
static bool m_wait_ready(void) {
    return present && selected;
}
static uint8_t m_xfer(uint8_t byte) {
    if(!present) return 0xFF;
    uint8_t out = 0x0F; /* chip status byte */
    if(phase == 0) {
        header = byte;
        uint8_t addr = byte & 0x3F;
        if(addr >= 0x30 && addr <= 0x3D && !(byte & CC1101_BURST)) {
            strobes[addr - 0x30]++;
            if(addr == CC1101_SRES) {
                memset(regs, 0, sizeof(regs));
                marcstate = CC1101_STATE_IDLE;
            }
            if(addr == CC1101_SIDLE) marcstate = CC1101_STATE_IDLE;
            if(addr == CC1101_SRX) marcstate = CC1101_STATE_RX;
            if(addr == CC1101_STX) marcstate = CC1101_STATE_TX;
            if(addr == CC1101_SPWD) powered_down = true;
        }
    } else {
        uint8_t addr = header & 0x3F;
        bool read = header & CC1101_READ, burst = header & CC1101_BURST;
        int idx = burst ? phase - 1 : 0;
        if(addr == CC1101_PATABLE) {
            if(read)
                out = patable[idx & 7];
            else
                patable[idx & 7] = byte;
        } else if(addr >= 0x30) {
            status_regs[CC1101_MARCSTATE - 0x30] = marcstate;
            out = (read && burst) ? status_regs[addr - 0x30] : 0;
        } else if(read) {
            out = regs[addr + idx];
        } else {
            regs[addr + idx] = byte;
        }
    }
    phase++;
    return out;
}
static void m_delay(uint32_t us) {
    (void)us;
}
static const Cc1101Bus bus = {m_select, m_wait_ready, m_xfer, m_delay};

static void test_probe(void) {
    status_regs[CC1101_VERSION - 0x30] = 0x14;
    status_regs[CC1101_PARTNUM - 0x30] = 0x00;
    CHECK(cc1101_probe(&bus));
    present = false; /* nothing on the bus: MISO never goes low */
    CHECK(!cc1101_probe(&bus));
    present = true;
    status_regs[CC1101_VERSION - 0x30] = 0xFF;
    CHECK(!cc1101_probe(&bus));
    status_regs[CC1101_VERSION - 0x30] = 0x14;
    CHECK(!selected); /* chip-select always released */
}

static void test_load_and_tune(void) {
    CHECK(cc1101_reset(&bus));
    CHECK_EQ(strobes[CC1101_SRES - 0x30], 1);
    for(size_t i = 0; i < sg_preset_count; i++) {
        const SgPreset* p = &sg_presets[i];
        cc1101_reset(&bus);
        cc1101_load(&bus, p->regs, p->patable);
        for(const uint8_t* r = p->regs; r[0] || r[1]; r += 2) CHECK_EQ(cc1101_read(&bus, r[0]), r[1]);
        CHECK(memcmp(patable, p->patable, 8) == 0);
        CHECK_EQ(regs[CC1101_IOCFG0], 0x0D); /* async data on GD0 */
        CHECK_EQ(regs[CC1101_PKTCTRL0] & 0x30, 0x30); /* asynchronous serial mode */
    }
    uint32_t actual = cc1101_set_frequency(&bus, 433920000);
    CHECK_EQ(regs[CC1101_FREQ2], 0x10);
    CHECK_EQ(regs[CC1101_FREQ1], 0xB0);
    CHECK_EQ(regs[CC1101_FREQ0], 0x71);
    CHECK(actual > 433919000 && actual < 433921000);
    actual = cc1101_set_frequency(&bus, 868350000);
    CHECK_EQ(regs[CC1101_FREQ2], 0x21);
    CHECK(actual > 868349000 && actual < 868351000);
}

static void test_states(void) {
    CHECK(cc1101_rx(&bus));
    CHECK_EQ(marcstate, CC1101_STATE_RX);
    CHECK(cc1101_tx(&bus));
    CHECK(cc1101_idle(&bus));
    cc1101_sleep(&bus);
    CHECK(powered_down);
    CHECK(!selected);

    status_regs[CC1101_RSSI - 0x30] = 0x80; /* -128 -> -138 dBm */
    CHECK_EQ(cc1101_rssi_dbm(&bus), -138);
    status_regs[CC1101_RSSI - 0x30] = 0x00;
    CHECK_EQ(cc1101_rssi_dbm(&bus), -74);
    status_regs[CC1101_RSSI - 0x30] = 0xC8; /* -56 -> -102 dBm */
    CHECK_EQ(cc1101_rssi_dbm(&bus), -102);
    status_regs[CC1101_RSSI - 0x30] = 0x30; /* 48 -> -50 dBm */
    CHECK_EQ(cc1101_rssi_dbm(&bus), -50);
}

int main(void) {
    RUN(test_probe);
    RUN(test_load_and_tune);
    RUN(test_states);
    TEST_END();
}
