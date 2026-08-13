# CMakeLists.txt for Graphics module.
# Only implementation files may enter the static library. The directory is
# currently header-only, so the target is skipped until real sources exist.
# (Building a library from a lone header makes `ar` fail with
# "no archive members specified".)

file(GLOB_RECURSE Graphics_SOURCES CONFIGURE_DEPENDS "*.c" "*.cpp" "*.cc" "*.mm")

if(Graphics_SOURCES)
    add_library(Graphics STATIC ${Graphics_SOURCES})
    target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(Graphics PROPERTIES LINKER_LANGUAGE CXX)
endif()
