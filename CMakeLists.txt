cmake_minimum_required(VERSION 3.20)

# A top-level configure of this directory needs a project() for CMake to have a
# language, a compiler, and a version at all. It is not a supported build: the
# dependency guard below aborts it, by design, with a named diagnostic. Nested
# NSX builds add this directory with add_subdirectory(), where the enclosing
# project already owns the language and toolchain setup, so declaring a nested
# project() there would start a second project scope and re-run compiler
# detection for no benefit.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(nsx_sensors VERSION 0.2.0 LANGUAGES C)
endif()

# Provides CMAKE_INSTALL_INCLUDEDIR, which the install interface and both
# install() rules below already reference. A consumer is not required to have
# included GNUInstallDirs first, and without it the variable expands empty, so
# the export interface and the headers land on a bare relative path.
include(GNUInstallDirs)

foreach(nsx_sensors_dependency nsx::core nsx::i2c nsx::spi)
    if(NOT TARGET ${nsx_sensors_dependency})
        message(FATAL_ERROR
            "nsx-sensors requires the ${nsx_sensors_dependency} target. Add this "
            "module from an NSX build that provides nsx-core, nsx-i2c, and nsx-spi."
        )
    endif()
endforeach()

# Vendored TDK eMD "common files + basic driver" subset. Listed explicitly so a
# newly vendored file is a reviewed change rather than an implicit build input;
# see PROVENANCE.md for the exact upstream subset and its exclusions.
set(nsx_sensors_vendor_sources
    ${CMAKE_CURRENT_LIST_DIR}/vendor/tdk-icm45605/inv_imu_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/vendor/tdk-icm45605/inv_imu_transport.c
)

add_library(nsx_sensors STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ina228.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ledstick.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_max86150.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_mpu6050.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_icm45605.c
    ${nsx_sensors_vendor_sources}
)

set_target_properties(nsx_sensors PROPERTIES EXPORT_NAME sensors)
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

install(TARGETS nsx_sensors EXPORT nsxTargets)

install(
    DIRECTORY includes-api/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-sensors/includes-api
)
