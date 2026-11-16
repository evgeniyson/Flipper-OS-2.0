# PC build: unit tests, the sublab command line analyser and the UI simulator.
add_compile_options(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

add_library(fos_portable STATIC ${FOS_PORTABLE})
target_include_directories(fos_portable PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(sublab tools/sublab.c)
target_link_libraries(sublab fos_portable)

enable_testing()
foreach(t test_core test_subghz test_vectors)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} fos_portable)
    add_test(NAME ${t} COMMAND ${t} ${CMAKE_SOURCE_DIR}/tests/vectors)
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
