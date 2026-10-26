# One compilation profile for cores, vendor sources, adapters and consumers.
# An empty selection auto-detects NSX, but leaves standalone builds independent.
if(NOT DEFINED NSX_SENSOR_COMPILE_CONTEXT)
    set(NSX_SENSOR_COMPILE_CONTEXT "" CACHE STRING
        "Interface target carrying the sensor CPU/FPU/ABI compilation profile")
endif()
set(nsx_sensor_compile_context "${NSX_SENSOR_COMPILE_CONTEXT}")
if(NOT nsx_sensor_compile_context AND TARGET nsx::board_flags)
    set(nsx_sensor_compile_context nsx::board_flags)
endif()
if(nsx_sensor_compile_context)
    if(NOT TARGET "${nsx_sensor_compile_context}")
        message(FATAL_ERROR
            "NSX_SENSOR_COMPILE_CONTEXT target '${nsx_sensor_compile_context}' must exist before adding sensors")
    endif()
    get_target_property(nsx_sensor_context_type "${nsx_sensor_compile_context}" TYPE)
    if(NOT nsx_sensor_context_type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "NSX_SENSOR_COMPILE_CONTEXT must name an INTERFACE library")
    endif()
    # The installed config reconnects this external target by its public name.
    # Do not require a firmware's framework targets to be exported with sensors.
    target_link_libraries(nsx_sensor_common PUBLIC
        "$<BUILD_INTERFACE:${nsx_sensor_compile_context}>")
endif()
