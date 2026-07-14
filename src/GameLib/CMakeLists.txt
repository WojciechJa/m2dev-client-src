file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

# Keep explicit DX11 extension file in list even with GLOB lag.
list(APPEND FILE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/MapOutdoorRenderDX11.cpp"
)
list(REMOVE_DUPLICATES FILE_SOURCES)

add_library(GameLib STATIC ${FILE_SOURCES})

if(EXISTS "${CMAKE_SOURCE_DIR}/extern/third_party/DirectXTK/Inc/DDSTextureLoader.h")
	target_include_directories(GameLib PRIVATE
		"${CMAKE_SOURCE_DIR}/extern/third_party/DirectXTK/Inc"
	)
endif()

target_link_libraries(GameLib
	lzo2
	mio
	RenderDX11
)

GroupSourcesByFolder(GameLib)
