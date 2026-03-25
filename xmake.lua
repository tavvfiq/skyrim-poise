-- set minimum xmake version
set_xmakever("2.8.2")

-- CommonLibSSE-NG integration (same pattern as souls-style-loot).
-- Expected layout:
--   lib/CommonLibSSE-NG/  (submodule or copy of your local CommonLibSSE-NG xmake package)
includes("lib/CommonLibSSE-NG")

-- project metadata
set_project("ChocolatePoise")
set_version("1.0.0")
set_license("MIT")

-- defaults
set_languages("c++23")
set_warnings("allextra")

-- build release with debug symbols so .pdb is generated (for crash debugging)
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
set_defaultmode("releasedbg")

-- third-party dependencies (non-CommonLib)
add_requires("nlohmann_json")
add_requires("simpleini")
add_requires("magic_enum")
add_requires("spdlog", {configs = {header_only = true}})

-- template config variables (used by cmake/*.in templates)
set_configvar("PROJECT_NAME", "ChocolatePoise")
set_configvar("PROJECT_VERSION", "1.0.0")
set_configvar("PROJECT_VERSION_MAJOR", 1)
set_configvar("PROJECT_VERSION_MINOR", 0)
set_configvar("PROJECT_VERSION_PATCH", 0)

target("ChocolatePoise")
    add_deps("commonlibsse-ng")

    add_rules("commonlibsse-ng.plugin", {
        name = "ChocolatePoise",
        author = "libxse",
        description = "ER-style poise for Skyrim"
    })

    add_packages("nlohmann_json", "simpleini", "magic_enum", "spdlog")

    -- generate Plugin.h and version.rc from the existing CMake templates
    add_configfiles("cmake/Plugin.h.in", {filename = "cmake/Plugin.h"})
    if is_plat("windows") then
        add_configfiles("cmake/Version.rc.in", {filename = "cmake/version.rc"})
        add_files("$(buildir)/cmake/version.rc")
    end

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_headerfiles("include/**.h")

    add_includedirs("src", "include", "$(buildir)/cmake", {public = true})

    set_pcxxheader("include/PCH.h")

