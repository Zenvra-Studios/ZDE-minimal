if(NOT BUILD_TESTING)
    return()
endif()

add_executable(ZDEUnitTests
    CommandRegistryTests.cpp
    LanguageServerTests.cpp
    SourceControlTests.cpp
)

target_link_libraries(ZDEUnitTests PRIVATE
    GTest::gtest_main
    GTest::gtest
    Zenvra::Application
    Zenvra::UI
    Zenvra::Language
    Zenvra::Terminal
    Zenvra::Tools
    Zenvra::Services
    Zenvra::SourceControl
    Zenvra::Platform
    Zenvra::Graphics
)
target_include_directories(ZDEUnitTests PRIVATE "${PROJECT_SOURCE_DIR}/Source" "${PROJECT_SOURCE_DIR}")
zde_enable_warnings(ZDEUnitTests)

add_test(NAME ZDE.UnitTests COMMAND ZDEUnitTests)
