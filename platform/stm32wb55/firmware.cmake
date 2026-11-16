# Firmware image for the Flipper Zero (STM32WB55RG, Cortex-M4).
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
add_compile_options(${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections -fno-common
                    -ffreestanding -fno-builtin-printf)
add_compile_definitions(STM32WB55xx)

set(FOS_APP
    core/input.c
    ui/canvas.c
    ui/font_data.c
    app/main.c
    app/ui.c
    app/options.c
    app/settings.c
    app/rx.c
    app/tx.c
    apps/menu.c
    apps/spectrum.c
    apps/finder.c
    apps/receiver.c
    apps/analyzer_view.c
    apps/recorder.c
    apps/saved.c
    apps/settings_app.c
)
set(FOS_PLATFORM
    drivers/cc1101.c
    platform/stm32wb55/startup.c
    platform/stm32wb55/mcu.c
    platform/stm32wb55/hal_board.c
    platform/stm32wb55/radio.c
    platform/stm32wb55/sd.c
    platform/stm32wb55/storage.c
    platform/stm32wb55/main.c
)
set(FOS_THIRD_PARTY third_party/fatfs/ff.c third_party/fatfs/ffunicode.c)
# third-party code is built as-is, without our warning policy
set_source_files_properties(${FOS_THIRD_PARTY} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(firmware.elf ${FOS_PORTABLE} ${FOS_APP} ${FOS_PLATFORM} ${FOS_THIRD_PARTY})
target_include_directories(firmware.elf PRIVATE
    ${CMAKE_SOURCE_DIR}
    platform/stm32wb55
    third_party/cmsis
    third_party/fatfs
)
target_link_options(firmware.elf PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/platform/stm32wb55/linker.ld
    -nostartfiles
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=firmware.map
    -Wl,--print-memory-usage
)
set_target_properties(firmware.elf PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/platform/stm32wb55/linker.ld)

add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary firmware.elf firmware.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/bin2dfu.py firmware.bin firmware.dfu
    COMMAND ${CMAKE_SIZE} firmware.elf
    COMMENT "firmware.bin")
