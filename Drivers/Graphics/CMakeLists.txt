# CMakeLists.txt for Graphics module.
find_package(OpenGL REQUIRED)

file(GLOB_RECURSE Graphics_SOURCES CONFIGURE_DEPENDS "*.c" "*.cpp" "*.cc" "*.mm")

if(Graphics_SOURCES)
    add_library(Graphics STATIC ${Graphics_SOURCES})
    add_library(Zenvra::Graphics ALIAS Graphics)
    target_include_directories(Graphics PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(Graphics PUBLIC OpenGL::GL)
    if(WIN32)
        target_link_libraries(Graphics PUBLIC opengl32)
    endif()
    zde_enable_warnings(Graphics)
endif()
