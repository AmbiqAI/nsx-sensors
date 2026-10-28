# Consume the same target/source definitions as standalone CMake and NSX.
if(CONFIG_NSX_SENSORS)
  if(CONFIG_MINIMAL_LIBC AND (CONFIG_NSX_SENSOR_BME688 OR
      CONFIG_NSX_SENSOR_INA228 OR CONFIG_NSX_SENSOR_ICM45605))
    message(FATAL_ERROR "Selected NSX math cores require a complete C library; select CONFIG_NEWLIB_LIBC=y or CONFIG_PICOLIBC=y")
  endif()
  foreach(driver BME688 LEDSTICK INA228 MPU6050 MAX86150 ICM42670 ICM45605 TSL254X T5838)
    set(NSX_SENSOR_${driver} OFF)
    if(CONFIG_NSX_SENSOR_${driver})
      set(NSX_SENSOR_${driver} ON)
    endif()
  endforeach()
  set(NSX_SENSOR_PORT_NSX OFF)
  set(NSX_SENSORS_TESTS OFF)
  set(NSX_SENSOR_COMPILE_CONTEXT zephyr_interface)
  # Zephyr loads modules before project()/compiler ABI discovery. GNUInstallDirs
  # cannot infer libdir yet; exports are unused here but retain a sane default.
  if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR lib)
  endif()
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/portable)
  if(CONFIG_I2C OR CONFIG_SPI)
    zephyr_library_named(nsx_sensor_port_zephyr)
    zephyr_library_sources(../src/ports/zephyr.c)
    zephyr_library_link_libraries(nsx_sensors)
    zephyr_library_sources_ifdef(CONFIG_NSX_SENSOR_BME688_ZEPHYR bme688.c)
  endif()
  # Application sources compile before final ELF linking. Export the direct
  # public include requirements from the shared targets (including vendor
  # context headers), without duplicating their authoritative directory lists.
  get_directory_property(sensor_targets DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
                         DEFINITION nsx_portable_targets)
  foreach(sensor_target IN LISTS sensor_targets)
    get_target_property(sensor_includes ${sensor_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(sensor_includes)
      zephyr_include_directories(${sensor_includes})
    endif()
  endforeach()
  zephyr_link_libraries(nsx_sensors)
endif()
