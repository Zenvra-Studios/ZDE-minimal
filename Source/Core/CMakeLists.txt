# CMakeLists.txt for Core module.

file(GLOB_RECURSE Core_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Core_SOURCES)
    add_library(Core STATIC ${Core_SOURCES})
    target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
