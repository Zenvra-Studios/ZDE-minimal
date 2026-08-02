# CMakeLists.txt for Scripts module.

file(GLOB_RECURSE Scripts_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Scripts_SOURCES)
    add_library(Scripts STATIC ${Scripts_SOURCES})
    target_include_directories(Scripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
