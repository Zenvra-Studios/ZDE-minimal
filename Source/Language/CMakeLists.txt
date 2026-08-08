# CMakeLists.txt for Language module.

file(GLOB_RECURSE Language_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Language_SOURCES)
    add_library(Language SHARED ${Language_SOURCES})
    target_include_directories(Language PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
