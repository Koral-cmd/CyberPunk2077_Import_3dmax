cmake_minimum_required(VERSION 3.20)
project(CP2077Import LANGUAGES CXX)
set(MAX_VERSION "0" CACHE STRING "3ds Max year, e.g. 2025")

# ------------------------------------------------------------
#  One core library, three targets:
#    cp77probe        the core without Max (diagnostics, any OS)
#    test_core        tests against synthetic WolvenKit output
#    CP2077Import.dlu the 3ds Max plugin (needs the Max SDK)
# ------------------------------------------------------------
# The 3ds Max 2026+ SDK headers expect C++20 (Autodesk's requirements page); older ones build with 17.
if(CP77_BUILD_PLUGIN_STD20 OR (MAX_VERSION GREATER_EQUAL 2026))
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(3DSMAX_SDK_DIR "" CACHE PATH "3ds Max SDK folder (maxsdk)")
option(CP77_BUILD_PLUGIN "Build the 3ds Max plugin (needs the SDK)" OFF)
option(CP77_BUILD_TOOLS "Build cp77probe and the tests" ON)

if(MSVC)
    # The plugin must use the DLL runtime in every configuration - Max and the
    # plugin share one heap.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_definitions(_UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
                            # VS 2022 17.10+: std::mutex would crash against an older msvcp140.dll inside Max
                            _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    add_compile_options(/EHsc /utf-8 /W4 /Zc:__cplusplus /wd4100)
else()
    add_compile_options(-Wall -Wextra -Wshadow)
endif()

set(CP77_CORE
    src/core/util.cpp src/core/json.cpp src/core/xform.cpp src/core/gltf.cpp
    src/core/red.cpp src/core/meshdata.cpp src/core/anim.cpp src/core/animprep.cpp
    src/core/settings.cpp src/core/wolvenkit.cpp src/core/assembler.cpp src/core/prep.cpp)

add_library(cp77core STATIC ${CP77_CORE})
target_include_directories(cp77core PUBLIC src/core)
if(MSVC AND CP77_BUILD_PLUGIN)
    # Max SDK structures need /Zp8; the core only exchanges its own types with
    # the plugin, but keep both the same to be safe.
    target_compile_options(cp77core PRIVATE /Zp8)
endif()

if(CP77_BUILD_TOOLS)
    add_executable(cp77probe tools/cp77probe.cpp)
    target_link_libraries(cp77probe PRIVATE cp77core)
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE cp77core)
endif()

if(CP77_BUILD_PLUGIN)
    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR "3ds Max SDK not found at '${3DSMAX_SDK_DIR}' - pass -D3DSMAX_SDK_DIR=...\\maxsdk")
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "3ds Max is 64-bit only: configure with -A x64")
    endif()
    add_library(CP2077Import SHARED
        src/max/plugin.h src/max/plugin_internal.h
        src/max/plugin_dll.cpp src/max/plugin_main.cpp src/max/plugin_scene.cpp src/max/plugin_anim.cpp
        src/max/CP2077Import.rc CP2077Import.def)
    set_target_properties(CP2077Import PROPERTIES SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "CP2077Import"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
    target_include_directories(CP2077Import SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(CP2077Import PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")
    target_link_libraries(CP2077Import PRIVATE cp77core core.lib maxutil.lib bmm.lib geom.lib mesh.lib mnmath.lib paramblk2.lib)
    if(MSVC)
        # /Zp8 is mandatory with the Max SDK (wrong packing = silently wrong structs)
        target_compile_options(CP2077Import PRIVATE /Zp8 /bigobj /external:W0)
        target_link_options(CP2077Import PRIVATE /OPT:REF /OPT:ICF)
    endif()
endif()

message(STATUS "CP2077Import: Max ${MAX_VERSION}, plugin=${CP77_BUILD_PLUGIN}, tools=${CP77_BUILD_TOOLS}")
