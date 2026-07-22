# Third-Party Notices

This repository contains only the independently maintained native C++ backend
for Fallout4WheelMenu. It does not contain FallUI, Wheel Menu - FallUIified,
ActionScript source, FLA projects, compiled SWF files, or icon assets.

## CommonLibF4 and commonlib-shared

This project is built against
[Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4)
at commit `ca31eeb6c7353555973bc351c6733d6492f2c66e`. CommonLibF4's top-level
code is distributed under the MIT License.

That revision statically links
[Dear-Modding-FO4/commonlib-shared](https://github.com/Dear-Modding-FO4/commonlib-shared)
at commit `f0b1670ee9caac2e349497f6f3c08a69633a8ea7`. `commonlib-shared` is
distributed under GPL-3.0 with a Modding Exception. The exception permits
Modded Code to link with `commonlib-shared` without causing that Modded Code to
be covered by the GPL. Project-authored source in this repository therefore
remains under the repository MIT License.

Copies of the applicable GPL-3.0 text and Modding Exception are retained in
`licenses/commonlib-shared/LICENSE` and `licenses/commonlib-shared/EXCEPTIONS`.

## Build dependencies

The following projects are obtained separately by the build system and are not
vendored in this repository:

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
