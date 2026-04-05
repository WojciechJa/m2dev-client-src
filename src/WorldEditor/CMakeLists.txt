if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "WorldEditor requires x64 toolchain (CMAKE_SIZEOF_VOID_P=8).")
endif()

file(GLOB_RECURSE WORLD_EDITOR_SOURCES CONFIGURE_DEPENDS
	"*.h"
	"*.hpp"
	"*.c"
	"*.cpp"
	"*.rc"
)

# This target is an MFC application (legacy WorldEditor project parity).
set(CMAKE_MFC_FLAG 1)
add_executable(WorldEditor WIN32 ${WORLD_EDITOR_SOURCES})

set_target_properties(WorldEditor PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_compile_definitions(WorldEditor PRIVATE
	WORLD_EDITOR
	_CRT_SECURE_NO_DEPRECATE
	_MBCS
)

if (MSVC)
	# Keep Visual Studio project character set in sync with legacy MBCS code and WinMain entry point.
	set_target_properties(WorldEditor PROPERTIES VS_GLOBAL_CharacterSet "MultiByte")

	# WorldEditor codebase is legacy MBCS-oriented; force-disable global Unicode macros for this target.
	target_compile_options(WorldEditor PRIVATE /UUNICODE /U_UNICODE)
	# Force MBCS startup symbol to avoid accidental Unicode startup mismatch in generated vcxproj.
	target_link_options(WorldEditor PRIVATE /ENTRY:WinMainCRTStartup)
endif()

target_include_directories(WorldEditor PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(WorldEditor PRIVATE
	AudioLib
	DebugUI
	EffectLib
	EterBase
	EterGrnLib
	EterImageLib
	EterLib
	EterLocale
	EterPythonLib
	GameLib
	PackLib
	PRTerrainLib
	ScriptLib
	SpeedTreeLib
	SphereLib

	DirectX
	Granny
	Python
	SpeedTree
	WebView

	comctl32
	shlwapi
	version
	ws2_32
)

GroupSourcesByFolder(WorldEditor)
