add_library(RenderDX11 INTERFACE)
add_library(RenderLegacyDX9 INTERFACE)
add_library(DirectX INTERFACE)

set(DIRECTXTK_ROOT "${CMAKE_SOURCE_DIR}/extern/third_party/DirectXTK")

if(EXISTS "${DIRECTXTK_ROOT}/Inc/DDSTextureLoader.h" AND EXISTS "${DIRECTXTK_ROOT}/Src/DDSTextureLoader.cpp")
	add_library(DirectXTK_DDS STATIC
		"${DIRECTXTK_ROOT}/Src/DDSTextureLoader.cpp"
	)

	target_include_directories(DirectXTK_DDS
		PUBLIC
			"${DIRECTXTK_ROOT}/Inc"
		PRIVATE
			"${DIRECTXTK_ROOT}/Src"
	)

	target_compile_definitions(DirectXTK_DDS PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_WIN32_WINNT=0x0A00
	)
endif()

# DirectXTK Helpers (CommonStates, BufferHelpers, GraphicsMemory) for B6/Phase2
if(EXISTS "${DIRECTXTK_ROOT}/Inc/CommonStates.h" AND EXISTS "${DIRECTXTK_ROOT}/Src/CommonStates.cpp")
	add_library(DirectXTK_Helpers STATIC
		"${DIRECTXTK_ROOT}/Src/CommonStates.cpp"
		"${DIRECTXTK_ROOT}/Src/BufferHelpers.cpp"
		"${DIRECTXTK_ROOT}/Src/GraphicsMemory.cpp"
	)

	target_include_directories(DirectXTK_Helpers
		PUBLIC
			"${DIRECTXTK_ROOT}/Inc"
		PRIVATE
			"${DIRECTXTK_ROOT}/Src"
	)

	target_compile_definitions(DirectXTK_Helpers PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_WIN32_WINNT=0x0A00
	)
endif()

if(EXISTS "${DIRECTXTK_ROOT}/Inc/WICTextureLoader.h"
	AND EXISTS "${DIRECTXTK_ROOT}/Src/WICTextureLoader.cpp"
	AND EXISTS "${DIRECTXTK_ROOT}/Src/PrimitiveBatch.cpp"
	AND EXISTS "${DIRECTXTK_ROOT}/Src/VertexTypes.cpp"
	AND EXISTS "${DIRECTXTK_ROOT}/Src/DirectXHelpers.cpp"
	AND EXISTS "${DIRECTXTK_ROOT}/Src/SimpleMath.cpp")
	add_library(DirectXTK_Render STATIC
		"${DIRECTXTK_ROOT}/Src/WICTextureLoader.cpp"
		"${DIRECTXTK_ROOT}/Src/PrimitiveBatch.cpp"
		"${DIRECTXTK_ROOT}/Src/VertexTypes.cpp"
		"${DIRECTXTK_ROOT}/Src/DirectXHelpers.cpp"
		"${DIRECTXTK_ROOT}/Src/SimpleMath.cpp"
	)

	target_include_directories(DirectXTK_Render
		PUBLIC
			"${DIRECTXTK_ROOT}/Inc"
		PRIVATE
			"${DIRECTXTK_ROOT}/Src"
	)

	target_compile_definitions(DirectXTK_Render PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_WIN32_WINNT=0x0A00
	)
endif()

target_include_directories(RenderDX11 INTERFACE "${CMAKE_SOURCE_DIR}/extern/include")
target_include_directories(RenderLegacyDX9 INTERFACE "${CMAKE_SOURCE_DIR}/extern/include")
target_include_directories(DirectX INTERFACE "${CMAKE_SOURCE_DIR}/extern/include")

if(TARGET DirectXTK_DDS)
	target_link_libraries(RenderDX11 INTERFACE DirectXTK_DDS)
endif()

if(TARGET DirectXTK_Helpers)
	target_link_libraries(RenderDX11 INTERFACE DirectXTK_Helpers)
endif()

if(TARGET DirectXTK_Render)
	target_link_libraries(RenderDX11 INTERFACE DirectXTK_Render)
endif()

target_link_libraries(RenderDX11 INTERFACE
	d3d11.lib
	dxgi.lib
	d3dcompiler.lib
	windowscodecs.lib
	ole32.lib
)

target_link_libraries(DirectX INTERFACE RenderDX11)
if(NOT DX11_STRICT_ONLY)
	target_link_libraries(DirectX INTERFACE RenderLegacyDX9)
endif()
