# CMakeLists.txt for Drivers/Audio subsystem.

file(GLOB_RECURSE Audio_SOURCES CONFIGURE_DEPENDS "*.hpp" "*.h" "*.c" "*.cpp" "*.cc" "*.mm")

if(Audio_SOURCES)
    add_library(DriversAudio STATIC ${Audio_SOURCES})
    add_library(Zenvra::Drivers::Audio ALIAS DriversAudio)
    add_library(Zenvra::Audio ALIAS DriversAudio)

    target_include_directories(DriversAudio PUBLIC
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/Source"
    )

    target_link_libraries(DriversAudio PUBLIC
        Zenvra::Media
        Zenvra::Drivers::Media
    )

    if(TARGET FFmpeg::FFmpeg)
        target_link_libraries(DriversAudio PUBLIC FFmpeg::FFmpeg)
    endif()

    zde_enable_warnings(DriversAudio)
endif()
