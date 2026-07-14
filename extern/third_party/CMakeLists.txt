cmake_minimum_required(VERSION 3.15)

# Third-party libraries

# DirectXTK - DirectX Tool Kit (existing)
# Already included elsewhere in the project

# DirectXTex is needed only by the experimental SpeedTree atlas tool. Keeping it
# out of the default client build avoids an undeclared dependency in CI.
if(BUILD_SPEEDTREE_ATLAS_PACKER)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DirectXTex/CMakeLists.txt")
        message(FATAL_ERROR "BUILD_SPEEDTREE_ATLAS_PACKER requires DirectXTex")
    endif()
    add_subdirectory(DirectXTex)
endif()
