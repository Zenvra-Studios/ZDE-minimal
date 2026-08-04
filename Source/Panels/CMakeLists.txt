# CMakeLists.txt for Panels module.

set(Panels_SOURCES
    # Add source files here as they are implemented.
)

if(Panels_SOURCES)
    add_library(Panels STATIC ${Panels_SOURCES})
    target_include_directories(Panels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
