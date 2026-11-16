# PC build: unit tests, the sublab command line analyser and the UI simulator.
add_compile_options(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

add_library(fos_portable STATIC ${FOS_PORTABLE} drivers/cc1101.c)
target_include_directories(fos_portable PUBLIC ${CMAKE_SOURCE_DIR})

# The firmware's own screens and logic, linked against simulated hardware.
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
add_executable(fos_sim ${FOS_APP} platform/host/sim.c)
target_link_libraries(fos_sim fos_portable)

add_executable(sublab tools/sublab.c)
target_link_libraries(sublab fos_portable)

enable_testing()
foreach(t test_core test_subghz test_cc1101 test_vectors)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} fos_portable)
    add_test(NAME ${t} COMMAND ${t} ${CMAKE_SOURCE_DIR}/tests/vectors)
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

find_program(PYTHON3 python3)
if(PYTHON3)
    add_test(NAME sim_e2e COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/sim_test.py $<TARGET_FILE:fos_sim>)
endif()
