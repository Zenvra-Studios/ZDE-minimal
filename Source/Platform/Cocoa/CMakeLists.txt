# CMakeLists.txt for Cocoa Platform

if(NOT APPLE)
    return()
endif()

file(GLOB_RECURSE PLATFORM_COCOA_SOURCES
    "Config/*.h" "Config/*.plist" "Config/*.xib" "Config/*.storyboard"
    "Runtime/*.h" "Runtime/*.cpp" "Runtime/*.mm"
    "Components/*.h" "Components/*.cpp" "Components/*.mm"
    "Utility/*.h" "Utility/*.cpp" "Utility/*.mm"
    "Event/*.h" "Event/*.cpp" "Event/*.mm"
    "*.h" "*.cpp" "*.mm"
)

add_library(PlatformCocoa STATIC ${PLATFORM_COCOA_SOURCES})
target_include_directories(PlatformCocoa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link against Apple's Cocoa Framework natively
find_library(COCOA_FRAMEWORK Cocoa)
target_link_libraries(PlatformCocoa PRIVATE ${COCOA_FRAMEWORK})
