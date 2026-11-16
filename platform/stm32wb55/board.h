#pragma once
/* Flipper Zero main board (hardware target 7): STM32WB55RG pin map and
 * low-level helpers. Only register definitions come from the vendor headers. */
#include "stm32wb55xx.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CPU_HZ 64000000u

typedef struct {
    GPIO_TypeDef* port;
    uint8_t pin;
} Pin;

/* buttons: all active low with pull-ups, except OK which is active high */
#define PIN_BTN_UP    ((Pin){GPIOB, 10})
#define PIN_BTN_DOWN  ((Pin){GPIOC, 6})
#define PIN_BTN_LEFT  ((Pin){GPIOB, 11})
#define PIN_BTN_RIGHT ((Pin){GPIOB, 12})
#define PIN_BTN_OK    ((Pin){GPIOH, 3})
#define PIN_BTN_BACK  ((Pin){GPIOC, 13})

/* SPI2: display and SD card */
#define PIN_SPI_D_SCK  ((Pin){GPIOD, 1})
#define PIN_SPI_D_MOSI ((Pin){GPIOB, 15})
#define PIN_SPI_D_MISO ((Pin){GPIOC, 2})
#define PIN_LCD_CS     ((Pin){GPIOC, 11})
#define PIN_LCD_RST    ((Pin){GPIOB, 0})
#define PIN_LCD_DC     ((Pin){GPIOB, 1})
#define PIN_SD_CS      ((Pin){GPIOC, 12})
#define PIN_SD_DETECT  ((Pin){GPIOC, 10})
#define PIN_SD_POWER   ((Pin){GPIOA, 3}) /* open drain, released = on */

/* SPI1: on-board CC1101 */
#define PIN_RF_SCK  ((Pin){GPIOA, 5})
#define PIN_RF_MISO ((Pin){GPIOB, 4})
#define PIN_RF_MOSI ((Pin){GPIOB, 5})
#define PIN_RF_CS   ((Pin){GPIOD, 0})
#define PIN_RF_GD0  ((Pin){GPIOA, 1}) /* TIM2_CH2 */
#define PIN_RF_SW   ((Pin){GPIOC, 4}) /* antenna path select, with CC1101 GD2 */

/* SPI1 remapped to the GPIO header: external CC1101 module */
#define PIN_EXT_SCK  ((Pin){GPIOB, 3}) /* header pin 5 */
#define PIN_EXT_MISO ((Pin){GPIOA, 6}) /* header pin 3 */
#define PIN_EXT_MOSI ((Pin){GPIOA, 7}) /* header pin 2 */
#define PIN_EXT_CS   ((Pin){GPIOA, 4}) /* header pin 4 */
#define PIN_EXT_GD0  ((Pin){GPIOB, 2}) /* header pin 6 */

/* I2C1: LED driver, charger, fuel gauge (8-bit addresses) */
#define PIN_I2C_SCL  ((Pin){GPIOA, 9})
#define PIN_I2C_SDA  ((Pin){GPIOA, 10})
#define I2C_LP5562   0x60
#define I2C_BQ25896  0xD6
#define I2C_BQ27220  0xAA

#define PIN_UART_TX ((Pin){GPIOB, 6}) /* header pin 13, debug log */
#define PIN_VIBRO   ((Pin){GPIOA, 8})
#define PIN_SPEAKER ((Pin){GPIOB, 8})
#define PIN_IR_TX   ((Pin){GPIOB, 9})

/* ---- GPIO ---- */
typedef enum { PIN_INPUT = 0, PIN_OUTPUT = 1, PIN_ALT = 2, PIN_ANALOG = 3 } PinMode;
typedef enum { PULL_NONE = 0, PULL_UP = 1, PULL_DOWN = 2 } PinPull;

void pin_init(Pin p, PinMode mode, PinPull pull, bool open_drain, uint8_t alt);
static inline void pin_write(Pin p, bool high) {
    p.port->BSRR = high ? (1u << p.pin) : (1u << (p.pin + 16));
}
static inline bool pin_read(Pin p) {
    return (p.port->IDR >> p.pin) & 1;
}

/* ---- time ---- */
void clock_init(void); /* 64 MHz from the 32 MHz crystal, SysTick at 1 kHz */
uint32_t millis(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

/* ---- buses ---- */
typedef enum { SPI_DIV4 = 1, SPI_DIV16 = 3, SPI_DIV32 = 4, SPI_DIV256 = 7 } SpiSpeed;
void spi_init(SPI_TypeDef* spi, SpiSpeed speed); /* mode 0, 8 bit, MSB first */
uint8_t spi_xfer(SPI_TypeDef* spi, uint8_t byte);
void i2c_init(void);
bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value);
bool i2c_read(uint8_t addr, uint8_t reg, uint8_t* data, size_t len);

/* ---- board services ---- */
void uart_init(void);
void uart_write(const char* text);
void lcd_init(void);
void lcd_flush(const uint8_t* buf);
void lcd_contrast(int offset);
void lcd_off(void);
void light_init(void);
void power_init(void);
void radio_init(void);
void storage_init(void);
/* Show a message on the display and stop; BACK restarts, LEFT enters DFU. */
void panic(const char* line1, const char* line2) __attribute__((noreturn));
/* Jump into the ROM USB bootloader. Works at any point, even before RAM is set up. */
void dfu_enter(void) __attribute__((noreturn));
void dfu_request_and_reset(void) __attribute__((noreturn));
void shutdown_request_and_reset(void) __attribute__((noreturn));
/* Watchdog: restarts the device if the main loop stops for ~16 s. */
void watchdog_start(void);
void watchdog_feed(void);
