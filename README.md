# Fallout4WheelMenu Native

Native F4SE/CommonLibF4 backend for Fallout4WheelMenu. The plugin replaces or
supplements the Favorites input, collects inventory data, and sends wheel,
favorite-slot, layout, hotkey, and condition information to a compatible
Scaleform UI.

Mod page and downloads: [Fallout4WheelMenu on Nexus Mods](https://www.nexusmods.com/fallout4/mods/105620)

## Source scope

This repository intentionally contains only the independently maintained C++
backend and its native configuration files.

It does not include:

- ActionScript source or decompiled ActionScript
- Adobe Animate FLA projects
- `WheelMenu.swf`
- FallUI or Wheel Menu - FallUIified assets
- icon libraries or fonts

A compatible UI package must be installed separately for the DLL to display a
wheel. The public source license applies only to files contained in this
repository.

## Features

- Favorites-menu replacement or independent hotkey activation
- Keyboard, mouse, and gamepad input
- Configurable time slowdown and pause behavior
- Inventory classification and tag blacklist support
- Favorite ring and category layout controls
- Independent wheel and side-list positioning and scaling
- Optional hint bar, center name, and condition bars
- ImGui configuration interface with English and Chinese localization
- Optional ConditionSystemFramework durability decoding
- Support for multiple Fallout 4 runtime generations through CommonLibF4

## Requirements

- Windows 10 or later
- Visual Studio 2022 Build Tools with Desktop development with C++
- Git
- XMake 3.0 or later
- [Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4)
- Fallout 4 Script Extender
- A compatible Wheel Menu Scaleform UI package

## Build

This project must be built against the
[Dear-Modding-FO4 CommonLibF4 fork](https://github.com/Dear-Modding-FO4/commonlibf4).
The build script downloads the pinned revision from that fork into `.deps/`
when needed, configures XMake, and builds the plugin:

```powershell
.\scripts\build.ps1
```

To use an existing checkout of the same CommonLibF4 fork:

```powershell
.\scripts\build.ps1 -CommonLibF4Path D:\path\to\commonlibf4
```

The native project can also be configured manually:

```powershell
$env:COMMONLIBF4_PATH = 'D:\path\to\commonlibf4'
xmake f -P . -y -m releasedbg
xmake -P . -y Fallout4WheelMenu
```

The dependency checkout must include its `commonlib-shared` submodule.

## Runtime configuration

The default INI and ImGui localization files are provided under `config/`.
Release packaging should place them at:

```text
Data/F4SE/Plugins/Fallout4WheelMenu.ini
Data/F4SE/Plugins/Fallout4WheelMenu/lang/en_US.json
Data/F4SE/Plugins/Fallout4WheelMenu/lang/zh_CN.json
```

The compiled plugin belongs at:

```text
Data/F4SE/Plugins/Fallout4WheelMenu.dll
```

## License

The project-authored C++ source in this repository is released under
GPL-3.0-only. Built binaries also contain `commonlib-shared` code distributed
under GPL-3.0 with the CommonLib Modding Exception. See `COPYRIGHT`,
`THIRD_PARTY_NOTICES.md`, and `licenses/commonlib-shared` for details.
