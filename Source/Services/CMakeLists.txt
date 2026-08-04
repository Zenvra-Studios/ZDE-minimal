# CMakeLists.txt for Services module.

set(Services_SOURCES
    # Add source files here as they are implemented.
)

if(Services_SOURCES)
    add_library(Services STATIC ${Services_SOURCES})
    target_include_directories(Services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
