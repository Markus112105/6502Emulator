cmake_minimum_required(VERSION 3.20)

project(6502Emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(emulator_core STATIC
    src/bus.cpp
    src/cpu.cpp
)
target_include_directories(emulator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MSVC)
    target_compile_options(emulator_core PUBLIC /W4 /permissive-)
else ()
    target_compile_options(emulator_core PUBLIC -Wall -Wextra -Wpedantic)
endif ()

add_executable(emulator
    src/main.cpp
)
target_link_libraries(emulator PRIVATE emulator_core)

enable_testing()

add_executable(cpu_tests
    tests/cpu_tests.cpp
    tests/integration_timing_tests.cpp
)
target_link_libraries(cpu_tests PRIVATE emulator_core)
target_include_directories(cpu_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_test(NAME cpu_tests COMMAND cpu_tests)

add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS cpu_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
