# CMakeLists.txt for Audio module.

file(GLOB_RECURSE Audio_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Audio_SOURCES)
    add_library(Audio STATIC ${Audio_SOURCES})
    target_include_directories(Audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
