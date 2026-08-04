# CMakeLists.txt for Workspace module.

set(Workspace_SOURCES
    # Add source files here as they are implemented.
)

if(Workspace_SOURCES)
    add_library(Workspace STATIC ${Workspace_SOURCES})
    target_include_directories(Workspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
