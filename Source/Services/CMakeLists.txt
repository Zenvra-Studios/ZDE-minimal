add_library(ZDEServices
    Shader/CpuShaderRasterizer.cpp
    Shader/ShaderCompiler.cpp
    Shader/ShaderRuntimeEngine.cpp
    Build/BuildService.cpp
    Execution/ExecutionService.cpp
    Output/OutputLogManager.cpp
)
add_library(Zenvra::Services ALIAS ZDEServices)

target_include_directories(ZDEServices PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(ZDEServices PUBLIC Zenvra::Core Zenvra::UI Zenvra::Tools)
zde_enable_warnings(ZDEServices)
