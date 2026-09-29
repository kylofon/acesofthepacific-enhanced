cmake_minimum_required(VERSION 3.24)
project(aotpport C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Faithful port of Aces of the Pacific (INTRO.EXE, PS.EXE, AP.EXE in one process), VGA with AdLib
# sound (PLAN.md decisions 1, 2, 7). The executables are read from the game folder at run time.

find_package(SDL3 REQUIRED CONFIG)

# Nuked-OPL3 (LGPL-2.1), built as its own library and linked in; see third_party/nuked-opl3/README.md.
add_library(nuked_opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC third_party/nuked-opl3)

# Coordinator files in src/, then one glob per module directory (PORTING.md "Module layout"): a module
# owner adds <module>/<file>.c as listed in port/modules.csv and it is built without touching this file.
set(AOTP_MODULES platform video sound shell_ui shell_data sim_flow sim_world render3d game enhanced)   # ENH: src/enhanced
file(GLOB AOTP_SOURCES CONFIGURE_DEPENDS src/*.c)
foreach(m ${AOTP_MODULES})
    file(GLOB m_sources CONFIGURE_DEPENDS src/${m}/*.c)
    list(APPEND AOTP_SOURCES ${m_sources})
endforeach()
# Developer aid for parallel implementers: leave out unfinished files of other modules, e.g.
# -DAOTP_EXCLUDE_REGEX="sim_world/ai_[a-z]+\\.c$|sim_flow/(cockpit|init)\\.c$" (empty = build everything).
set(AOTP_EXCLUDE_REGEX "" CACHE STRING "regex of source files left out of the build")
if(AOTP_EXCLUDE_REGEX)
    list(FILTER AOTP_SOURCES EXCLUDE REGEX "${AOTP_EXCLUDE_REGEX}")
endif()

add_executable(aotp_enhanced ${AOTP_SOURCES})
set_target_properties(aotp_enhanced PROPERTIES OUTPUT_NAME "aotp-enhanced")
target_include_directories(aotp_enhanced PRIVATE src)
target_link_libraries(aotp_enhanced PRIVATE SDL3::SDL3 nuked_opl3)
if(MSVC)
    target_compile_options(aotp_enhanced PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(aotp_enhanced PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(aotp_enhanced PRIVATE -Wno-format-truncation)
    endif()
endif()
# ENH: the host renderer of the enhanced view (src/enhanced) runs per displayed frame at the output
# resolution: always optimised, whatever the build type (the faithful engine keeps the project's flags).
file(GLOB AOTP_ENH_SOURCES CONFIGURE_DEPENDS src/enhanced/*.c)
if(MSVC)
    set_source_files_properties(${AOTP_ENH_SOURCES} PROPERTIES COMPILE_OPTIONS "/O2")
else()
    set_source_files_properties(${AOTP_ENH_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
endif()
if(WIN32)
    add_custom_command(TARGET aotp_enhanced POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:aotp_enhanced> $<TARGET_FILE_DIR:aotp_enhanced>
        COMMAND_EXPAND_LISTS)
endif()

# The launcher ("Aces of the Pacific Enhanced"): the game folder, the game speed, sound and display options;
# built beside aotp-enhanced.exe. It needs wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw) and a C++17
# compiler.
option(AOTP_LAUNCHER "Also build the launcher, Aces of the Pacific Enhanced.exe (wxWidgets)" ON)
if(AOTP_LAUNCHER)
    add_subdirectory(launcher)
endif()
