cmake_minimum_required(VERSION 3.15)
project(SpeedTreeAtlasPacker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use DirectXTK (already in project) instead of DirectXTex
# DirectXTK has DDSTextureLoader which is sufficient for our needs

# Source files
set(SOURCES
    src/main.cpp
    src/AtlasPacker.cpp
    src/TextureLoader.cpp
    src/BinPacker.cpp
)

set(HEADERS
    src/AtlasPacker.h
    src/TextureLoader.h
    src/BinPacker.h
)

# Create executable
add_executable(SpeedTreeAtlasPacker ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(SpeedTreeAtlasPacker PRIVATE
    d3d11.lib
    dxgi.lib
    dxguid.lib
)

# Include directories
target_include_directories(SpeedTreeAtlasPacker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/third_party/DirectXTK/Inc
)

# Set output directory
set_target_properties(SpeedTreeAtlasPacker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)

message(STATUS "SpeedTreeAtlasPacker tool configured")
