# CMakeLists.txt for X11 Platform

if(NOT UNIX)
    return()
endif()

find_package(X11 REQUIRED)

# Collect all sources recursively within the X11 platform folder
file(GLOB_RECURSE PLATFORM_X11_SOURCES
    "Config/*.h" "Config/*.xml"
    "Runtime/*.h" "Runtime/*.cpp"
    "Components/*.h" "Components/*.cpp"
    "Utility/*.h" "Utility/*.cpp"
    "Event/*.h" "Event/*.cpp"
    "*.h" "*.cpp"
)

# Create a static library for the X11 Platform subsystem
add_library(PlatformX11 STATIC ${PLATFORM_X11_SOURCES})

# Specify include directories so other modules can find headers
target_include_directories(PlatformX11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link against the X11 library
target_link_libraries(PlatformX11 PRIVATE X11::X11)
