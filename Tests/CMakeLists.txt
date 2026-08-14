if(NOT BUILD_TESTING)
    return()
endif()

add_executable(ZDEUnitTests
    CommandRegistryTests.cpp
)

target_link_libraries(ZDEUnitTests PRIVATE Zenvra::Application Zenvra::UI Zenvra::Terminal Zenvra::Graphics)
target_include_directories(ZDEUnitTests PRIVATE "${PROJECT_SOURCE_DIR}/Source" "${PROJECT_SOURCE_DIR}")
zde_enable_warnings(ZDEUnitTests)

add_test(NAME ZDE.UnitTests COMMAND ZDEUnitTests)
