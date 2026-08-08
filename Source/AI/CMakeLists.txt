# CMakeLists.txt for AI module.

file(GLOB_RECURSE AI_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(AI_SOURCES)
    add_library(AI SHARED ${AI_SOURCES})
    target_include_directories(AI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
