# CMakeLists.txt for Tests module.

file(GLOB_RECURSE Tests_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Tests_SOURCES)
    add_library(Tests STATIC ${Tests_SOURCES})
    target_include_directories(Tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
