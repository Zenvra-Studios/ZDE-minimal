# CMakeLists.txt for Editors module.

set(Editors_SOURCES
    # Add source files here as they are implemented.
)

if(Editors_SOURCES)
    add_library(Editors STATIC ${Editors_SOURCES})
    target_include_directories(Editors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
