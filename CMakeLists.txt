cmake_minimum_required(VERSION 3.20)

project(nsx_sensors LANGUAGES C CXX ASM)

file(GLOB nsx_sensors_vendor_sources CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/vendor/tdk-icm45605/*.c
)

add_library(nsx_sensors STATIC
    src/nsx_ina228.c
    src/nsx_ledstick.c
    src/nsx_max86150.c
    src/nsx_mpu6050.c
    src/nsx_icm45605.c
    ${nsx_sensors_vendor_sources}
)

add_library(nsx::sensors ALIAS nsx_sensors)

target_include_directories(nsx_sensors
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/includes-api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-sensors/includes-api>
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/vendor/tdk-icm45605
)

target_link_libraries(nsx_sensors
    PUBLIC
        nsx::core
        nsx::i2c
        nsx::spi
)

target_compile_features(nsx_sensors PUBLIC c_std_11)

install(TARGETS nsx_sensors EXPORT nsx_sensorsTargets)

install(
    DIRECTORY includes-api/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-sensors/includes-api
)