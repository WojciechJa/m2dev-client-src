# DebugUI Module - ImGui Developer Monitoring Tool
# DX11 native only - no DX9 fallback

cmake_minimum_required(VERSION 3.20)

# DebugUI Library
add_library(DebugUI STATIC
    ImGuiManager.cpp
    ImGuiManager.h
    ImGuiMetricsCollector.cpp
    ImGuiMetricsCollector.h
    ImGuiGraphicsMetrics.cpp
    ImGuiGraphicsMetrics.h
    ImGuiGraphPlotter.cpp
    ImGuiGraphPlotter.h
)

target_include_directories(DebugUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/EterLib
    ${CMAKE_SOURCE_DIR}/UserInterface
)

# Link ImGui libraries (DX11 backend only - strict mode)
target_link_libraries(DebugUI PUBLIC
    imgui
    imgui_backend_dx11
    imgui_backend_win32
)

# DebugUI must stay library-only and never link against the UserInterface executable.
# UserInterface links DebugUI, so this remains one-way to avoid EXE<->LIB cycles in CMake/VS.
target_link_libraries(DebugUI PUBLIC
    EterLib
)

# DX11 Backend only - no DX9 dependency
if(WIN32)
    target_link_libraries(DebugUI PRIVATE
        d3d11.lib
    )
endif()

# C++20 standard
set_target_properties(DebugUI PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Preprocessor definitions
target_compile_definitions(DebugUI PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE>
)

# Warning level
if(MSVC)
    target_compile_options(DebugUI PRIVATE /W4)
endif()

# Windows specific
if(WIN32)
    target_compile_definitions(DebugUI PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
