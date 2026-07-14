cmake_minimum_required(VERSION 3.19)

project(m2dev-client-src)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ASan support
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_DX11_RENDERER "Enable DX11 renderer migration scaffolding" ON)
option(DX11_STRICT_ONLY "Enable strict DX11-only migration mode (disables DX9 legacy link in DirectX interface)" ON)
option(BUILD_DEBUG_UI "Build the optional ImGui diagnostics overlay" OFF)
option(BUILD_WORLDEDITOR "Build the separate WorldEditor target" OFF)
option(BUILD_SPEEDTREE_ATLAS_PACKER "Build the experimental SpeedTree atlas tool" OFF)
set(SHADER_CACHE_VERSION "1" CACHE STRING "Shader cache version used by runtime config")

set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	"${CMAKE_CURRENT_SOURCE_DIR}/buildtool"
)

include(Utils)

set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo;Release" CACHE STRING "Configurations" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options(/MP)

if(MSVC)
	add_compile_definitions(UNICODE _UNICODE)
endif()

add_compile_options(
	$<$<CXX_COMPILER_ID:MSVC>:/wd4828>
	$<$<CXX_COMPILER_ID:MSVC>:/wd4996>
	$<$<CXX_COMPILER_ID:MSVC>:/wd5033>
)

# ASan flags
if(ENABLE_ASAN)
	if(MSVC)
		add_compile_options(/fsanitize=address)
		add_link_options(/fsanitize=address)
		add_definitions(-D_DISABLE_VECTOR_ANNOTATION)
		add_definitions(-D_DISABLE_STRING_ANNOTATION)
	else()
		add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
		add_link_options(-fsanitize=address)
	endif()
endif()

add_definitions(-DNOMINMAX)
add_definitions(-DWIN32_LEAN_AND_MEAN)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DWINDOWS_IGNORE_PACKING_MISMATCH)

add_definitions(-DUSE_LOD)
add_definitions(-DDUNGEON_WORK)
add_definitions(-DBUILDING_GRANNY_STATIC)
add_definitions(-DGRANNY_THREADED)
add_definitions(-DPy_NO_ENABLE_SHARED)

add_compile_definitions("$<$<CONFIG:Debug>:DEBUG>")
add_compile_definitions("$<$<CONFIG:RelWithDebInfo>:_DISTRIBUTE>")
add_compile_definitions("$<$<CONFIG:Release>:_DISTRIBUTE>")
add_compile_definitions(SHADER_CACHE_VERSION=${SHADER_CACHE_VERSION})

if(ENABLE_DX11_RENDERER)
	add_compile_definitions(ENABLE_DX11_RENDERER=1)
endif()

if(DX11_STRICT_ONLY)
	add_compile_definitions(DX11_STRICT_ONLY=1)
endif()

if(BUILD_DEBUG_UI)
	add_compile_definitions(BUILD_DEBUG_UI=1)
endif()

include_directories("src")
include_directories("extern/include")
include_directories("vendor/libsodium/src/libsodium/include")
include_directories("vendor/freetype-2.13.3/include")
include_directories("vendor/imgui")
include_directories("vendor/imgui/backends")

add_custom_target(dx11_strict_gate
	COMMAND ${CMAKE_COMMAND} -E echo "DX11 strict migration gate scan"
	COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/dx11_strict_scan.ps1" -RepoRoot "${CMAKE_CURRENT_SOURCE_DIR}" -ManifestPath "${CMAKE_CURRENT_SOURCE_DIR}/tools/dx11_strict_scan_manifest.txt"
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(DX11_STRICT_ONLY)
	# Enforce strict scan during normal builds when strict mode is enabled.
	add_custom_target(dx11_strict_gate_all ALL DEPENDS dx11_strict_gate)
endif()

# Dependency targets must exist before first-party libraries link them.
add_subdirectory(vendor)
add_subdirectory(extern)
add_subdirectory(src)
add_subdirectory(tools)
