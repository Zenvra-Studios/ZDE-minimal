# CMakeLists.txt for Explorer module.

file(GLOB_RECURSE Explorer_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Explorer_SOURCES)
    add_library(Explorer STATIC ${Explorer_SOURCES})
    target_include_directories(Explorer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
