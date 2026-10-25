cmake_minimum_required(VERSION 3.20)
# Configure the portable aggregate nested in an NSX-like target graph.
# Consumer-link tests live in portable_build.sh; real SDK tests remain separate.
project(nsx_sensors_nested_contract LANGUAGES C)

set(NSX_SENSORS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "nsx-sensors source root")

# SDK stand-ins for automatic NSX adapter selection.
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
if(NOT nsx_sensors_type STREQUAL "INTERFACE_LIBRARY")
    message(FATAL_ERROR "expected a INTERFACE_LIBRARY, got '${nsx_sensors_type}'")
endif()

get_target_property(nsx_sensors_links nsx_sensors INTERFACE_LINK_LIBRARIES)
if(NOT nsx_sensors_links MATCHES "nsx_sensor_icm45605")
    message(FATAL_ERROR "aggregate omits portable ICM-45605")
endif()

message(STATUS "nsx-sensors nested contract harness configured")
