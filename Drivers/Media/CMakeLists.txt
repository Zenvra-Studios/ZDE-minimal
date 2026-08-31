# CMakeLists.txt for Drivers/Media subsystem.

file(GLOB_RECURSE Media_SOURCES CONFIGURE_DEPENDS "*.hpp" "*.cpp")

if(Media_SOURCES)
    add_library(DriversMedia STATIC ${Media_SOURCES})
    add_library(Zenvra::Drivers::Media ALIAS DriversMedia)
    add_library(Zenvra::Drivers::FFmpeg ALIAS DriversMedia)

    target_include_directories(DriversMedia PUBLIC
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/Source"
        "${CMAKE_SOURCE_DIR}/ThirdParty/ffmpeg/include"
    )

    target_link_libraries(DriversMedia PUBLIC
        Zenvra::Media
        Zenvra::Drivers::Audio
    )
    if(TARGET FFmpeg::FFmpeg)
        target_link_libraries(DriversMedia PUBLIC FFmpeg::FFmpeg)
    endif()

    zde_enable_warnings(DriversMedia)
endif()
