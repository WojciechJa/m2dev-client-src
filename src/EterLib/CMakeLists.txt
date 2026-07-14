file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

add_library(EterLib STATIC ${FILE_SOURCES})

# Enable DX11 shader cache
target_compile_definitions(EterLib PRIVATE DX11_SHADER_CACHE_ENABLED)

target_include_directories(EterLib
	PRIVATE
		${CMAKE_SOURCE_DIR}/extern/third_party/DirectXTK/Inc
		${CMAKE_SOURCE_DIR}/extern/include/directx9_old_notused
)

target_link_libraries(EterLib
	sodium
	mio
	freetype
	RenderDX11
)

GroupSourcesByFolder(EterLib)
