# CMakeLists.txt for Audio module.
# Only implementation files may enter the static library. The directory is
# currently header-only, so the target is skipped until real sources exist.
# (Building a library from a lone header makes `ar` fail with
# "no archive members specified".)

file(GLOB_RECURSE Audio_SOURCES CONFIGURE_DEPENDS "*.c" "*.cpp" "*.cc" "*.mm")

if(Audio_SOURCES)
    add_library(Audio STATIC ${Audio_SOURCES})
    target_include_directories(Audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(Audio PROPERTIES LINKER_LANGUAGE CXX)
endif()
