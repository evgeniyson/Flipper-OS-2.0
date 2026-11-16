/* Display, keys, lights and power: the hal/hal.h side of the board. */
#include "board.h"
#include "../../core/input.h"
#include "../../hal/hal.h"
#include "../../ui/canvas.h"

/* ------------------------------------------------------------ display (ST7567, 128x64) */

#define LCD_CONTRAST_ERC 32
#define LCD_CONTRAST_MGG 28
static uint8_t lcd_base_contrast = LCD_CONTRAST_ERC;

static void lcd_cmds(const uint8_t* cmds, size_t n) {
    spi_init(SPI2, SPI_DIV16); /* 4 MHz; the SD card may have changed the speed */
    pin_write(PIN_LCD_DC, false);
    pin_write(PIN_LCD_CS, false);
    for(size_t i = 0; i < n; i++) spi_xfer(SPI2, cmds[i]);
    pin_write(PIN_LCD_CS, true);
}

/* The factory writes the display variant into the one-time-programmable area. */
static bool lcd_is_mgg(void) {
    const uint8_t* otp = (const uint8_t*)OTP_AREA_BASE;
    bool v2 = otp[0] == 0xBE && otp[1] == 0xBA && otp[2] == 2;
    return v2 && otp[12] == 2;
}

void lcd_init(void) {
    pin_write(PIN_LCD_CS, true);
    pin_init(PIN_LCD_CS, PIN_OUTPUT, PULL_NONE, false, 0);
    pin_init(PIN_LCD_DC, PIN_OUTPUT, PULL_NONE, false, 0);
    pin_write(PIN_LCD_RST, false);
    pin_init(PIN_LCD_RST, PIN_OUTPUT, PULL_NONE, false, 0);
    pin_init(PIN_SPI_D_SCK, PIN_ALT, PULL_NONE, false, 5);
    pin_init(PIN_SPI_D_MOSI, PIN_ALT, PULL_NONE, false, 5);
    pin_init(PIN_SPI_D_MISO, PIN_ALT, PULL_UP, false, 5);

    delay_ms(2);
    pin_write(PIN_LCD_RST, true);
    delay_ms(2);

    bool mgg = lcd_is_mgg();
    lcd_base_contrast = mgg ? LCD_CONTRAST_MGG : LCD_CONTRAST_ERC;
    const uint8_t init[] = {
        0xE2, /* software reset */
        0xA2, /* bias 1/9 */
        0xA0, /* segment direction: normal */
        0xC8, /* common direction: reversed */
        0x40, /* start line 0 */
        (uint8_t)(0x20 | (mgg ? 6 : 5)), /* regulation ratio */
        0x81, lcd_base_contrast, /* electronic volume */
        0x2F, /* booster, regulator and follower on */
    };
    lcd_cmds(init, sizeof(init));
    delay_ms(2);
    const uint8_t on[] = {0xA4 /* normal display */, 0xAF /* display on */};
    lcd_cmds(on, sizeof(on));
}

void lcd_flush(const uint8_t* buf) {
    for(uint8_t page = 0; page < 8; page++) {
        const uint8_t addr[] = {(uint8_t)(0xB0 | page), 0x10, 0x00};
        lcd_cmds(addr, sizeof(addr));
        pin_write(PIN_LCD_DC, true);
        pin_write(PIN_LCD_CS, false);
        for(int x = 0; x < 128; x++) spi_xfer(SPI2, buf[page * 128 + x]);
        pin_write(PIN_LCD_CS, true);
    }
}

void lcd_contrast(int offset) {
    int value = lcd_base_contrast + offset;
    if(value < 0) value = 0;
    if(value > 63) value = 63;
    const uint8_t cmds[] = {0x81, (uint8_t)value};
    lcd_cmds(cmds, sizeof(cmds));
}

void lcd_off(void) {
    const uint8_t cmds[] = {0xAE /* display off */, 0xA5 /* all points on: lowest power */};
    lcd_cmds(cmds, sizeof(cmds));
}

void hal_display_flush(const uint8_t* buf) {
    lcd_flush(buf);
}
void hal_display_contrast(int offset) {
    lcd_contrast(offset);
}

/* ------------------------------------------------------------ lights (LP5562) */

void light_init(void) {
    i2c_write_reg(I2C_LP5562, 0x0D, 0xFF); /* reset */
    i2c_write_reg(I2C_LP5562, 0x07, 50); /* LED currents in 0.1 mA: red */
    i2c_write_reg(I2C_LP5562, 0x06, 50); /* green */
    i2c_write_reg(I2C_LP5562, 0x05, 50); /* blue */
    i2c_write_reg(I2C_LP5562, 0x0F, 150); /* white: display backlight */
    for(uint8_t reg = 0x02; reg <= 0x04; reg++) i2c_write_reg(I2C_LP5562, reg, 0);
    i2c_write_reg(I2C_LP5562, 0x0E, 0);
    i2c_write_reg(I2C_LP5562, 0x00, 0xC0); /* chip enable, logarithmic dimming */
    delay_us(600);
    i2c_write_reg(I2C_LP5562, 0x08, 0x61); /* internal clock, power save, fast PWM */
    i2c_write_reg(I2C_LP5562, 0x70, 0x00); /* all channels under direct I2C control */
}

void hal_backlight(uint8_t level) {
    static int last = -1;
    if(level == last) return;
    last = level;
    i2c_write_reg(I2C_LP5562, 0x0E, level);
}

void hal_led(uint8_t r, uint8_t g, uint8_t b) {
    static uint32_t last = 0xFFFFFFFFu;
    uint32_t now = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    if(now == last) return;
    last = now;
    i2c_write_reg(I2C_LP5562, 0x04, r);
    i2c_write_reg(I2C_LP5562, 0x03, g);
    i2c_write_reg(I2C_LP5562, 0x02, b);
}

/* ------------------------------------------------------------ keys */

static void keys_init(void) {
    pin_init(PIN_BTN_UP, PIN_INPUT, PULL_UP, false, 0);
    pin_init(PIN_BTN_DOWN, PIN_INPUT, PULL_UP, false, 0);
    pin_init(PIN_BTN_LEFT, PIN_INPUT, PULL_UP, false, 0);
    pin_init(PIN_BTN_RIGHT, PIN_INPUT, PULL_UP, false, 0);
    pin_init(PIN_BTN_BACK, PIN_INPUT, PULL_UP, false, 0);
    pin_init(PIN_BTN_OK, PIN_INPUT, PULL_DOWN, false, 0);
}

uint8_t hal_keys(void) {
    uint8_t mask = 0;
    if(!pin_read(PIN_BTN_UP)) mask |= 1u << KEY_UP;
    if(!pin_read(PIN_BTN_DOWN)) mask |= 1u << KEY_DOWN;
    if(!pin_read(PIN_BTN_LEFT)) mask |= 1u << KEY_LEFT;
    if(!pin_read(PIN_BTN_RIGHT)) mask |= 1u << KEY_RIGHT;
    if(!pin_read(PIN_BTN_BACK)) mask |= 1u << KEY_BACK;
    if(pin_read(PIN_BTN_OK)) mask |= 1u << KEY_OK;
    return mask;
}

/* ------------------------------------------------------------ power (BQ25896 charger, BQ27220 gauge) */

static bool charger_update(uint8_t reg, uint8_t clear, uint8_t set) {
    uint8_t v;
    if(!i2c_read(I2C_BQ25896, reg, &v, 1)) return false;
    return i2c_write_reg(I2C_BQ25896, reg, (uint8_t)((v & ~clear) | set));
}

void power_init(void) {
    /* Unused actuators stay off, also while the MCU is in shutdown. */
    PWR->CR3 |= PWR_CR3_APC;
    PWR->PDCRA |= PWR_PDCRA_PA8; /* vibration motor */
    PWR->PDCRB |= PWR_PDCRB_PB8 | PWR_PDCRB_PB9; /* speaker, infrared LED */
    pin_write(PIN_VIBRO, false);
    pin_init(PIN_VIBRO, PIN_OUTPUT, PULL_DOWN, false, 0);
    pin_init(PIN_SPEAKER, PIN_ANALOG, PULL_DOWN, false, 0);
    pin_init(PIN_IR_TX, PIN_ANALOG, PULL_DOWN, false, 0);
    keys_init();

    /* Same charger setup as the stock firmware: defaults, continuous ADC
     * conversion, no I2C watchdog (it would revert our settings), and the
     * 5 V boost limits used for the GPIO header. */
    charger_update(0x14, 0, 0x80); /* register reset */
    delay_ms(2);
    charger_update(0x02, 0, 0xC0); /* CONV_START | CONV_RATE */
    charger_update(0x07, 0x30, 0); /* WATCHDOG = disabled */
    charger_update(0x0A, 0xF7, 0x83); /* BOOSTV = 5.062 V, BOOST_LIM = 1.4 A */
    charger_update(0x03, 0x20, 0); /* 5 V boost off until the user enables it */
}

bool hal_battery(HalBattery* out) {
    uint8_t soc[2], mv[2], status;
    /* the gauge is only read, never written: its calibration stays untouched */
    if(!i2c_read(I2C_BQ27220, 0x2C, soc, 2) || !i2c_read(I2C_BQ27220, 0x08, mv, 2)) return false;
    out->percent = soc[0] > 100 ? 100 : soc[0];
    out->millivolts = (uint16_t)(mv[0] | mv[1] << 8);
    out->charging = i2c_read(I2C_BQ25896, 0x0B, &status, 1) && ((status >> 3) & 3) != 0 &&
                    ((status >> 3) & 3) != 3;
    return true;
}

void hal_ext_5v(bool on) {
    charger_update(0x03, 0x20, on ? 0x20 : 0); /* OTG_CONFIG: boost converter */
}

void radio_panic_stop(void);

void hal_power_off(void) {
    radio_panic_stop();
    hal_ext_5v(false);
    hal_led(0, 0, 0);
    hal_backlight(0);
    lcd_off();
    /* On battery this cuts the supply: the charger disconnects the cell
     * ("ship mode"); pressing BACK reconnects it. */
    charger_update(0x09, 0, 0x20); /* BATFET_DIS */
    delay_ms(200);
    /* Still running, so USB is powering us: put the MCU to sleep instead.
     * That is done from a clean reset (see startup.c). */
    shutdown_request_and_reset();
}

void hal_reboot(void) {
    radio_panic_stop();
    NVIC_SystemReset();
    for(;;) {
    }
}

void hal_reboot_dfu(void) {
    radio_panic_stop();
    hal_backlight(0);
    lcd_off();
    dfu_request_and_reset();
}

/* ------------------------------------------------------------ time, log, panic */

uint32_t hal_millis(void) {
    return millis();
}
void hal_delay_ms(uint32_t ms) {
    delay_ms(ms);
}
void hal_idle(void) {
    watchdog_feed();
    __WFI(); /* the 1 kHz tick wakes us at the latest */
}
void hal_log(const char* text) {
    uart_write(text);
    uart_write("\r\n");
}

void panic(const char* line1, const char* line2) {
    __disable_irq();
    radio_panic_stop();
    /* we may get here before the board is set up: bring up what the message needs */
    uart_init();
    i2c_init();
    lcd_init();
    keys_init();
    uart_write("\r\nPANIC: ");
    uart_write(line1);
    uart_write(" / ");
    uart_write(line2);
    uart_write("\r\n");
    cv_clear();
    cv_text(2, 2, &font_normal, "Firmware stopped");
    cv_hline(0, 12, CV_W);
    cv_text(2, 18, &font_small, line1);
    cv_text(2, 27, &font_small, line2);
    cv_text(2, 46, &font_small, "BACK: restart");
    cv_text(2, 54, &font_small, "LEFT: firmware update (DFU)");
    lcd_flush(cv_buffer());
    i2c_write_reg(I2C_LP5562, 0x0E, 120);
    i2c_write_reg(I2C_LP5562, 0x04, 80); /* red LED */
    /* wait for the key that caused nothing: require a fresh press */
    delay_ms(300);
    for(;;) {
        uint8_t keys = hal_keys();
        if(keys & (1u << KEY_BACK)) NVIC_SystemReset();
        if(keys & (1u << KEY_LEFT)) dfu_request_and_reset();
        watchdog_feed(); /* keep the message on screen */
        delay_ms(10);
    }
}
