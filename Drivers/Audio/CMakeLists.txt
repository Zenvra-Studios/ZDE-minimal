# CMakeLists.txt for Audio module.

set(Audio_SOURCES
    AudioDevice.h
)

if(Audio_SOURCES)
    add_library(Audio STATIC ${Audio_SOURCES})
    target_include_directories(Audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(Audio PROPERTIES LINKER_LANGUAGE CXX)
endif()
