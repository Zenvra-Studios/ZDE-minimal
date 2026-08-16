add_library(ZDETools
    Builder/CMakeBuilder.cpp
    Runner/ProcessRunner.cpp
    Debugger/DebuggerEngine.cpp
)
add_library(Zenvra::Tools ALIAS ZDETools)

target_include_directories(ZDETools PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(ZDETools PUBLIC Zenvra::Core)
zde_enable_warnings(ZDETools)
