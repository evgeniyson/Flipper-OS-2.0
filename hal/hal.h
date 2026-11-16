#pragma once
/* Hardware abstraction: everything the portable firmware needs from a board.
 * Implemented by platform/stm32wb55 (the Flipper Zero) and platform/host
 * (the PC simulator). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- time ---- */
uint32_t hal_millis(void);
void hal_delay_ms(uint32_t ms);
/* Wait for the next interrupt (device) or advance simulated time (PC). */
void hal_idle(void);

/* ---- display ---- */
/* buf: 8 pages x 128 columns, see ui/canvas.h */
void hal_display_flush(const uint8_t* buf);
void hal_display_contrast(int offset); /* -8..+8 around the factory value */
void hal_backlight(uint8_t level); /* 0..255 */
void hal_led(uint8_t r, uint8_t g, uint8_t b);

/* ---- input ---- */
/* bit n set = key n (see core/input.h) is held down */
uint8_t hal_keys(void);

/* ---- power ---- */
typedef struct {
    uint8_t percent;
    bool charging;
    uint16_t millivolts;
} HalBattery;
bool hal_battery(HalBattery* out);
void hal_ext_5v(bool on); /* 5 V on the GPIO header, for amplified radio modules */
void hal_power_off(void);
void hal_reboot(void);
void hal_reboot_dfu(void); /* into the ROM USB bootloader, for flashing */

/* ---- debug ---- */
void hal_log(const char* text);
