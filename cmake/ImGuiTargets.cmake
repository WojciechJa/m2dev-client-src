set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/vendor/imgui")

add_library(imgui STATIC
	"${IMGUI_ROOT}/imgui.cpp"
	"${IMGUI_ROOT}/imgui.h"
	"${IMGUI_ROOT}/imgui_demo.cpp"
	"${IMGUI_ROOT}/imgui_draw.cpp"
	"${IMGUI_ROOT}/imgui_internal.h"
	"${IMGUI_ROOT}/imgui_tables.cpp"
	"${IMGUI_ROOT}/imgui_widgets.cpp"
	"${IMGUI_ROOT}/imstb_rectpack.h"
	"${IMGUI_ROOT}/imstb_textedit.h"
	"${IMGUI_ROOT}/imstb_truetype.h"
)

target_include_directories(imgui PUBLIC "${IMGUI_ROOT}")
target_compile_features(imgui PUBLIC cxx_std_20)
target_compile_definitions(imgui PRIVATE
	$<$<CONFIG:Debug>:_DEBUG>
	$<$<CONFIG:Release>:NDEBUG>
)

add_library(imgui_backend_dx11 STATIC
	"${IMGUI_ROOT}/backends/imgui_impl_dx11.cpp"
	"${IMGUI_ROOT}/backends/imgui_impl_dx11.h"
)
target_link_libraries(imgui_backend_dx11 PUBLIC imgui PRIVATE d3d11.lib)
target_include_directories(imgui_backend_dx11 PUBLIC
	"${IMGUI_ROOT}"
	"${IMGUI_ROOT}/backends"
)

add_library(imgui_backend_win32 STATIC
	"${IMGUI_ROOT}/backends/imgui_impl_win32.cpp"
	"${IMGUI_ROOT}/backends/imgui_impl_win32.h"
)
target_link_libraries(imgui_backend_win32 PUBLIC imgui)
target_include_directories(imgui_backend_win32 PUBLIC
	"${IMGUI_ROOT}"
	"${IMGUI_ROOT}/backends"
)

if(MSVC)
	target_compile_options(imgui PRIVATE /W3)
	target_compile_options(imgui_backend_dx11 PRIVATE /W3)
	target_compile_options(imgui_backend_win32 PRIVATE /W3)
endif()
