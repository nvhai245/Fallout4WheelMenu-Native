set_xmakever("3.0.0")

set_project("Fallout4WheelMenu")
set_version("1.0.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_policy("package.requires_lock", true)

local commonlibf4_path = os.getenv("COMMONLIBF4_PATH")
if not commonlibf4_path or not os.isdir(commonlibf4_path) then
    raise("COMMONLIBF4_PATH must point to a CommonLibF4 checkout")
end

includes(commonlibf4_path)

add_requires("simpleini v4.25")
add_requires("nlohmann_json v3.12.0")
add_requires("tinyxml2 11.0.0")
add_requires("imgui v1.92.5-docking", { configs = { win32 = true, dx11 = true } })
add_requires("microsoft-detours 2023.6.8")

target("Fallout4WheelMenu")
    add_rules("commonlibf4.plugin", {
        name = "Fallout4WheelMenu",
        author = "h_wushen",
        description = "Native F4SE backend for Fallout4WheelMenu",
        version = "1.0.0"
    })

    add_packages("simpleini", "nlohmann_json", "tinyxml2", "imgui", "microsoft-detours")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("src", "include")
    set_pcxxheader("src/pch.h")
    set_encodings("utf-8")
    add_cxxflags("/utf-8", { force = true, tools = { "msvc", "clang-cl" } })

    add_defines(
        'PLUGIN_NAME="Fallout4WheelMenu"',
        "PLUGIN_VERSION_MAJOR=1",
        "PLUGIN_VERSION_MINOR=0",
        "PLUGIN_VERSION_PATCH=0"
    )
