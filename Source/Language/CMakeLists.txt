# CMakeLists.txt for Language module.

set(Language_SOURCES
    LanguageConfiguration.h
    Server/LanguageServer.cpp
    Server/LanguageServer.h
)

if(Language_SOURCES)
    add_library(Language STATIC ${Language_SOURCES})
    target_include_directories(Language PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
