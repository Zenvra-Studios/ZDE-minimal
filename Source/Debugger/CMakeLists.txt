# CMakeLists.txt for Debugger module.

set(Debugger_SOURCES
    # Add source files here as they are implemented.
)

if(Debugger_SOURCES)
    add_library(Debugger STATIC ${Debugger_SOURCES})
    target_include_directories(Debugger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
