# CMakeLists.txt for Services module.

file(GLOB_RECURSE Services_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Services_SOURCES)
    add_library(Services SHARED ${Services_SOURCES})
    target_include_directories(Services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
