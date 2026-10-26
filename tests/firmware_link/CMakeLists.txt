cmake_minimum_required(VERSION 3.20)
project(sensor_firmware_link_contract LANGUAGES C CXX)
# Architecture/link fixture only: no startup code, board, SDK, or hardware claim.
add_library(profile INTERFACE)
target_compile_options(profile INTERFACE
    -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
    -fshort-enums -ffunction-sections -fdata-sections)
target_link_options(profile INTERFACE
    -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
    -nostartfiles --specs=nosys.specs -Wl,--gc-sections -Wl,-e,main)
add_library(test::profile ALIAS profile)
set(NSX_SENSOR_COMPILE_CONTEXT test::profile)
set(NSX_SENSOR_PORT_NSX OFF CACHE BOOL "" FORCE)
add_subdirectory(../.. sensors)
# Real API calls force each selected archive's implementation into the ELF.
add_executable(firmware ../portable/headers.cpp)
# The C++ consumer only calls C APIs; no C++ runtime is needed by this fixture.
set_target_properties(firmware PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(firmware PRIVATE nsx::sensors)
# Merely linking the aggregate must not satisfy the checker if code is unused.
add_executable(unused unused.c)
target_link_libraries(unused PRIVATE nsx::sensors)
add_executable(data_symbol data.c)
target_link_libraries(data_symbol PRIVATE nsx::sensors)
