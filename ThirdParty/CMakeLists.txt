# CMakeLists.txt for ThirdParty module.

file(GLOB_RECURSE ThirdParty_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(ThirdParty_SOURCES)
    add_library(ThirdParty STATIC ${ThirdParty_SOURCES})
    target_include_directories(ThirdParty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
