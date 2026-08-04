# CMakeLists.txt for Server module.

file(GLOB_RECURSE Server_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Server_SOURCES)
    add_library(Server STATIC ${Server_SOURCES})
    target_include_directories(Server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
