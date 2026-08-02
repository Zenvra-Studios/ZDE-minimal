# CMakeLists.txt for Application module.

file(GLOB_RECURSE Application_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Application_SOURCES)
    add_library(Application STATIC ${Application_SOURCES})
    target_include_directories(Application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
