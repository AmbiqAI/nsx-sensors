# Shared CMake compilation settings

The root CMake file defines portable driver targets once. NSX bare-metal and
FreeRTOS builds use those same targets; no RTOS-specific driver library is needed.
Zephyr integration remains pending.

Every translation unit using a public sensor type must agree on CPU/FPU and C
ABI settings, including enum size. Linking a framework target only to an
application does not apply its compilation settings to the application's static
library dependencies.

When `nsx::board_flags` exists before adding this module, all sensor targets
inherit it through the common target. This includes vendor sources and the NSX
adapter, even when only individual sensor targets are linked. It also applies
when the NSX transport adapter is disabled. Standalone builds without that target
continue to use their toolchain settings without an SDK dependency.

Other integrations can select a compilation profile explicitly:

```cmake
add_library(platform_profile INTERFACE)
# Populate with the application's CPU/FPU/ABI settings or link a framework's
# existing interface target. Do not copy device source lists into this target.
add_library(platform::profile ALIAS platform_profile)
set(NSX_SENSOR_COMPILE_CONTEXT platform::profile)
add_subdirectory(third_party/nsx-sensors)
target_link_libraries(app PRIVATE nsx::sensor_bme688)
```

`NSX_SENSOR_COMPILE_CONTEXT` must name an existing interface library. An empty
value selects `nsx::board_flags` when available, otherwise no additional profile.
An explicit target takes precedence. Its transitive usage requirements apply to
cores and consumers. Keep the profile free of sensor dependencies (no cycles)
and peripheral/runtime libraries; this is compilation configuration, not a HAL.
Define/select it before adding sensors and rebuild all objects after changing it.

## Installed packages

A package built with a compilation profile records the target's public name.
Before `find_package(nsx_sensors)`, create or load that target with the same
toolchain/CPU/FPU/ABI settings as the producer. Discovery fails with an actionable
message if it is missing. The package reconnects it to imported sensor targets;
the framework does not need to export its targets alongside the sensor package.
`nsx_sensors_COMPILE_CONTEXT` exposes the recorded name (empty for a build without
a profile). The consumer cannot override it using the source-build selection.

Target existence is not a binary-compatibility check: a different profile using
the same name is still incompatible. Packages built without a profile likewise
require matching producer/consumer toolchain settings. A host archive is not an
embedded firmware archive. No stable cross-toolchain or cross-ABI binary layout
is promised for public contexts or configurations.

`tests/compile_context.sh` builds actual core/vendor/adapter targets under
transitive short-enum and normal-enum profiles, checks C/C++ layout and real API
calls, and exercises separately built relocated consumers and missing-profile
errors. These are host contract tests, not real firmware or hardware qualification.
