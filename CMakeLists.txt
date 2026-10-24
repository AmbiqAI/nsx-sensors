cmake_minimum_required(VERSION 3.20)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(nsx_sensors VERSION 0.3.0 LANGUAGES C)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(NSX_SENSOR_BME688 "Build portable BME688" ON)
option(NSX_SENSOR_INA228 "Build portable INA228" ON)
option(NSX_SENSOR_LEDSTICK "Build portable LED Stick" ON)
# NSX resolves and adds its declared dependencies before this module. Preserve
# the metadata-advertised aggregate there, without requiring an SDK in portable
# consumers or discovering dependencies from sibling directories.
set(nsx_sensors_legacy_default OFF)
if(TARGET nsx::core AND TARGET nsx::i2c AND TARGET nsx::spi)
    set(nsx_sensors_legacy_default ON)
endif()
option(NSX_SENSORS_LEGACY_NSX "Build remaining NSX-only drivers (transitional)"
    ${nsx_sensors_legacy_default})
option(NSX_SENSOR_PORT_NSX "Build the shared NSX transport adapter" ${nsx_sensors_legacy_default})
option(NSX_SENSOR_INSTALL_NSX_PORT "Export the NSX port (requires SDK package exports)" OFF)
option(NSX_SENSORS_TESTS "Build portable host tests" OFF)

add_library(nsx_sensor_common STATIC src/common/bus.c)
add_library(nsx::sensor_common ALIAS nsx_sensor_common)
set_target_properties(nsx_sensor_common PROPERTIES EXPORT_NAME sensor_common)
target_include_directories(nsx_sensor_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(nsx_sensor_common PUBLIC c_std_11)
set(nsx_portable_targets nsx_sensor_common)
if(NSX_SENSOR_BME688)
    add_library(nsx_sensor_bme688 STATIC src/bme688/bme688.c src/bme688/compensation.c)
    add_library(nsx::sensor_bme688 ALIAS nsx_sensor_bme688)
    set_target_properties(nsx_sensor_bme688 PROPERTIES EXPORT_NAME sensor_bme688)
    target_link_libraries(nsx_sensor_bme688 PUBLIC nsx_sensor_common)
    if(NOT MSVC)
        target_link_libraries(nsx_sensor_bme688 PUBLIC m)
    endif()
    list(APPEND nsx_portable_targets nsx_sensor_bme688)
endif()
if(NSX_SENSOR_LEDSTICK)
    add_library(nsx_sensor_ledstick STATIC src/ledstick/ledstick.c)
    add_library(nsx::sensor_ledstick ALIAS nsx_sensor_ledstick)
    set_target_properties(nsx_sensor_ledstick PROPERTIES EXPORT_NAME sensor_ledstick)
    target_link_libraries(nsx_sensor_ledstick PUBLIC nsx_sensor_common)
    list(APPEND nsx_portable_targets nsx_sensor_ledstick)
endif()
if(NSX_SENSOR_INA228)
    add_library(nsx_sensor_ina228 STATIC src/ina228/ina228.c)
    add_library(nsx::sensor_ina228 ALIAS nsx_sensor_ina228)
    set_target_properties(nsx_sensor_ina228 PROPERTIES EXPORT_NAME sensor_ina228)
    target_link_libraries(nsx_sensor_ina228 PUBLIC nsx_sensor_common)
    if(NOT MSVC)
        target_link_libraries(nsx_sensor_ina228 PUBLIC m)
    endif()
    list(APPEND nsx_portable_targets nsx_sensor_ina228)
endif()
install(TARGETS ${nsx_portable_targets} EXPORT nsxSensorTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/nsx/sensors/status.h include/nsx/sensors/bus.h
    include/nsx/sensors/runtime.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/sensors)
foreach(driver bme688 ledstick ina228)
    if(TARGET nsx_sensor_${driver})
        install(FILES include/nsx/sensors/${driver}.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/sensors)
    endif()
endforeach()
install(EXPORT nsxSensorTargets NAMESPACE nsx:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nsx_sensors)
configure_package_config_file(cmake/nsx_sensorsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nsx_sensorsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nsx_sensors)
# Do not use PROJECT_VERSION: in a nested build that belongs to the consumer.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" nsx_sensors_version LIMIT_COUNT 1)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/nsx_sensorsConfigVersion.cmake
    VERSION ${nsx_sensors_version} COMPATIBILITY ExactVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nsx_sensorsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nsx_sensorsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nsx_sensors)
install(FILES LICENSE NOTICE licenses/Bosch-BME68x-BSD-3-Clause.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nsx/modules/nsx-sensors/licenses)

# Keep SDK-dependent targets outside the portable package export closure.
if(NSX_SENSOR_PORT_NSX)
    foreach(dependency nsx::i2c nsx::spi)
        if(NOT TARGET ${dependency})
            message(FATAL_ERROR "NSX sensor port requires ${dependency}")
        endif()
    endforeach()
    add_library(nsx_sensor_port_nsx STATIC src/ports/nsx.c)
    add_library(nsx::sensor_port_nsx ALIAS nsx_sensor_port_nsx)
    set_target_properties(nsx_sensor_port_nsx PROPERTIES EXPORT_NAME sensor_port_nsx)
    target_link_libraries(nsx_sensor_port_nsx PUBLIC nsx_sensor_common nsx::i2c nsx::spi)
    # Firmware builds consume SDK source targets without exporting an SDK
    # package. Emitting this export there makes CMake reject its dependencies.
    # Package producers opt in after providing the SDK install export closure.
    if(NSX_SENSOR_INSTALL_NSX_PORT)
        install(TARGETS nsx_sensor_port_nsx EXPORT nsxSensorNsxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(EXPORT nsxSensorNsxTargets NAMESPACE nsx::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nsx_sensors)
        install(FILES include/nsx/sensors/port_nsx.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/sensors)
    endif()
endif()

# Transitional NSX aggregate contains only the three unmigrated drivers.
# BME688/LED Stick/INA228 now use their portable targets; link nsx::sensor_port_nsx for NSX transport.
if(NSX_SENSORS_LEGACY_NSX)
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

endif()

if(NSX_SENSORS_TESTS)
    enable_testing()
    add_subdirectory(tests/portable)
endif()
