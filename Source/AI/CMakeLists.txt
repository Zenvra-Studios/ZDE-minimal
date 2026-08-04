# CMakeLists.txt for AI module.

set(AI_SOURCES
    # Add source files here as they are implemented.
)

if(AI_SOURCES)
    add_library(AI STATIC ${AI_SOURCES})
    target_include_directories(AI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
