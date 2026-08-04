# CMakeLists.txt for ThirdParty module.

set(ThirdParty_SOURCES
    # Add source files here as they are implemented.
)

if(ThirdParty_SOURCES)
    add_library(ThirdParty STATIC ${ThirdParty_SOURCES})
    target_include_directories(ThirdParty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
