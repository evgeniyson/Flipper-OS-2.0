#include "board.h"
#include "../../app/main.h"

int main(void) {
    clock_init();
    watchdog_start();
    uart_init();
    uart_write("\r\nFlipper OS 2 starting\r\n");
    i2c_init();
    power_init(); /* charger, keys, unused actuators */
    lcd_init();
    light_init();
    radio_init();
    storage_init();
    fos_start();
    for(;;) fos_step();
}
