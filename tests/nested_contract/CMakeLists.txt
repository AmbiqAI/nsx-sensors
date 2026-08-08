cmake_minimum_required(VERSION 3.20)

# Nested-consumer contract harness for nsx-sensors.
#
# This stands in for an NSX application so the nesting contract can be checked
# without an SDK, a board, or a cross toolchain. It asserts what a real NSX
# build depends on:
#
#   * add_subdirectory() of the module works and does not open a nested project
#     scope, so PROJECT_NAME stays the consumer's;
#   * the nsx::sensors alias, the nsx_sensors target, and EXPORT_NAME sensors
#     all exist;
#   * the target exports into the shared nsxTargets export set;
#   * the public include interface resolves the installed header layout.
#
# It deliberately does NOT compile the drivers: they need real NSX transport and
# AmbiqSuite headers. Compiling is the job of tests/configure_target_smoke.sh
# (real consumer, real toolchain) and tests/vendor_compile_smoke.sh (vendored
# subset). Keeping this harness dependency-free is what lets it run everywhere.

project(nsx_sensors_nested_contract LANGUAGES C)

set(NSX_SENSORS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "nsx-sensors source root")

# Minimal stand-ins for the transport modules an NSX build supplies. INTERFACE
# libraries are enough because this harness only configures. They install into
# the same shared nsxTargets export set that the real nsx-i2c and nsx-spi use,
# so the export closure below is representative.
foreach(nsx_stub core i2c spi)
    add_library(nsx_stub_${nsx_stub} INTERFACE)
    set_target_properties(nsx_stub_${nsx_stub} PROPERTIES EXPORT_NAME ${nsx_stub})
    add_library(nsx::${nsx_stub} ALIAS nsx_stub_${nsx_stub})
    install(TARGETS nsx_stub_${nsx_stub} EXPORT nsxTargets)
endforeach()

add_subdirectory("${NSX_SENSORS_ROOT}" nsx-sensors)

if(NOT TARGET nsx_sensors)
    message(FATAL_ERROR "nsx_sensors target was not defined")
endif()
if(NOT TARGET nsx::sensors)
    message(FATAL_ERROR "nsx::sensors alias was not defined")
endif()

# A nested add_subdirectory() must not start a second project. project() only
# rebinds PROJECT_NAME inside the subdirectory's own scope, so read the variable
# there rather than here.
get_directory_property(nsx_sensors_project
    DIRECTORY "${NSX_SENSORS_ROOT}"
    DEFINITION PROJECT_NAME
)
if(NOT nsx_sensors_project STREQUAL "nsx_sensors_nested_contract")
    message(FATAL_ERROR
        "nested add_subdirectory() opened project '${nsx_sensors_project}'; "
        "the module declared a project() outside a top-level configure"
    )
endif()
if(NOT CMAKE_PROJECT_NAME STREQUAL "nsx_sensors_nested_contract")
    message(FATAL_ERROR
        "nested add_subdirectory() changed CMAKE_PROJECT_NAME to "
        "'${CMAKE_PROJECT_NAME}'"
    )
endif()

get_target_property(nsx_sensors_export_name nsx_sensors EXPORT_NAME)
if(NOT nsx_sensors_export_name STREQUAL "sensors")
    message(FATAL_ERROR
        "expected EXPORT_NAME 'sensors', got '${nsx_sensors_export_name}'"
    )
endif()

get_target_property(nsx_sensors_type nsx_sensors TYPE)
if(NOT nsx_sensors_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "expected a STATIC_LIBRARY, got '${nsx_sensors_type}'")
endif()

get_target_property(nsx_sensors_includes nsx_sensors INTERFACE_INCLUDE_DIRECTORIES)
if(NOT nsx_sensors_includes MATCHES "includes-api")
    message(FATAL_ERROR
        "public include interface does not expose includes-api: "
        "'${nsx_sensors_includes}'"
    )
endif()

# The module must be exportable through the shared nsxTargets set a real NSX
# package config installs. This fails at generate time if the module used a
# private export set instead.
install(EXPORT nsxTargets NAMESPACE nsx:: DESTINATION lib/cmake/nsx)

message(STATUS "nsx-sensors nested contract harness configured")
