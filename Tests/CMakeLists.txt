if(NOT BUILD_TESTING)
    return()
endif()

add_executable(ZDEUnitTests
    CommandRegistryTests.cpp
    LanguageServerTests.cpp
    TerminalCompatTests.cpp
    FFmpegTests.cpp
    ShaderServiceTests.cpp
    # SourceControlTests.cpp
)

target_link_libraries(ZDEUnitTests PRIVATE
    GTest::gtest_main
    GTest::gtest
    Zenvra::Application
    Zenvra::UI
    Zenvra::Language
    Zenvra::Terminal
    Zenvra::Utility
    Zenvra::Media
    Zenvra::Drivers::Media
    Zenvra::Drivers::Audio
    Zenvra::Tools
    Zenvra::Services
    # Zenvra::SourceControl
    Zenvra::Platform
    Zenvra::Graphics
)
target_include_directories(ZDEUnitTests PRIVATE "${PROJECT_SOURCE_DIR}/Source" "${PROJECT_SOURCE_DIR}")
zde_enable_warnings(ZDEUnitTests)

add_test(NAME ZDE.UnitTests COMMAND ZDEUnitTests)

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/ffmpeg/bin")
    file(GLOB FFMPEG_TEST_DLLS "${CMAKE_SOURCE_DIR}/ThirdParty/ffmpeg/bin/*.dll")
    if(FFMPEG_TEST_DLLS)
        add_custom_command(TARGET ZDEUnitTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FFMPEG_TEST_DLLS}
                "$<TARGET_FILE_DIR:ZDEUnitTests>"
            COMMENT "Deploying FFmpeg runtime DLLs to unit test binary directory..."
        )
    endif()
endif()

