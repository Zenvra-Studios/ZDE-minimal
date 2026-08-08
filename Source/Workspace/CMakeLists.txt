# CMakeLists.txt for Workspace module.

file(GLOB_RECURSE Workspace_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Workspace_SOURCES)
    add_library(Workspace SHARED ${Workspace_SOURCES})
    target_include_directories(Workspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
