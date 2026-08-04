# CMakeLists.txt for Server module.

set(Server_SOURCES
    # Add source files here as they are implemented.
)

if(Server_SOURCES)
    add_library(Server STATIC ${Server_SOURCES})
    target_include_directories(Server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
