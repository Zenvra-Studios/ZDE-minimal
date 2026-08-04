# CMakeLists.txt for Config module.

set(Config_SOURCES
    # Add source files here as they are implemented.
)

if(Config_SOURCES)
    add_library(Config STATIC ${Config_SOURCES})
    target_include_directories(Config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
