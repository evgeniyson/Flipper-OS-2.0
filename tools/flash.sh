#!/bin/sh
# Flash the firmware over USB. Put the Flipper into DFU mode first:
#   hold LEFT + BACK until it restarts, release BACK, keep LEFT held
#   (with this firmware running you can also use Settings -> Reboot to DFU).
# If the firmware does not start at all: hold OK + BACK for 30 seconds,
# release BACK, then OK - that enters the hardware bootloader.
set -eu
BIN="${1:-build/stm32/firmware.bin}"
[ -f "$BIN" ] || { echo "not found: $BIN (build the firmware first)"; exit 1; }
exec dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D "$BIN"
