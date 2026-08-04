# CMakeLists.txt for Scripts module.

set(Scripts_SOURCES
    # Add source files here as they are implemented.
)

if(Scripts_SOURCES)
    add_library(Scripts STATIC ${Scripts_SOURCES})
    target_include_directories(Scripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
