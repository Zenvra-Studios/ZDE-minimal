# CMakeLists.txt for Explorer module.

set(Explorer_SOURCES
    # Add source files here as they are implemented.
)

if(Explorer_SOURCES)
    add_library(Explorer STATIC ${Explorer_SOURCES})
    target_include_directories(Explorer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
