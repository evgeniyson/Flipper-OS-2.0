cmake_minimum_required(VERSION 3.16)
project(flipper_os2 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror -Wshadow -Wno-unused-parameter)

# Everything that does not touch hardware: built for the device and for the PC.
set(FOS_PORTABLE
    core/fmt.c
    subghz/packet.c
    subghz/protocols.c
    subghz/proto_dynamic.c
    subghz/proto_weather.c
    subghz/analyzer.c
    subghz/sub_file.c
    subghz/presets.c
    subghz/region.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    include(platform/stm32wb55/firmware.cmake)
else()
    include(platform/host/host.cmake)
endif()
