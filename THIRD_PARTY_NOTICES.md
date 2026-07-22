# Third-Party Notices

This repository contains only the independently maintained native C++ backend
for Fallout4WheelMenu. It does not contain FallUI, Wheel Menu - FallUIified,
ActionScript source, FLA projects, compiled SWF files, or icon assets.

## Build dependencies

The following projects are obtained separately by the build system and are not
vendored in this repository:

- CommonLibF4 by Dear Modding, MIT License.
- Dear ImGui, MIT License.
- Microsoft Detours, MIT License.
- SimpleIni, MIT License.
- JSON for Modern C++, MIT License.
- TinyXML-2, zlib License.

Refer to each dependency's source distribution for its complete license text.

## Project history

This native backend is designed to interoperate with a compatible Wheel Menu
Scaleform UI. The original Fallout 4 Wheel Menu concept and UI were created by
devpieron. FallUI integration was created separately by m8r98a4f2. No source or
binary assets from either UI distribution are included here.

## ConditionSystemFramework interface

`include/ConditionSystem/DurabilityAPI.h` is maintained by the same project
author and is included under this repository's MIT License so the optional
condition display integration can be built without a sibling source checkout.
