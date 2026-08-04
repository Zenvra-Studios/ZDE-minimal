# CMakeLists.txt for Graphics module.

set(Graphics_SOURCES
    shaders/BackdropBlur.h
)

if(Graphics_SOURCES)
    add_library(Graphics STATIC ${Graphics_SOURCES})
    target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(Graphics PROPERTIES LINKER_LANGUAGE CXX)
endif()
