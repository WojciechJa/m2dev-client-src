file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

# Exclude SpeedGrassRT.cpp - it's legacy code that doesn't compile with modern DX11
# The DX11 grass implementation uses GenerateGrassVertices in SpeedGrassWrapper.cpp instead
list(FILTER FILE_SOURCES EXCLUDE REGEX ".*SpeedGrassRT\\.cpp$")

add_library(SpeedTreeLib STATIC ${FILE_SOURCES})

target_link_libraries(SpeedTreeLib
	mio
)

GroupSourcesByFolder(SpeedTreeLib)
