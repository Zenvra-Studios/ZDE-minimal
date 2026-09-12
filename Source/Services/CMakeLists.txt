add_library(ZDEServices
    Shader/CpuShaderRasterizer.cpp
    Shader/GpuShaderRasterizer.cpp
    Shader/ShaderCompiler.cpp
    Shader/ShaderRuntimeEngine.cpp
    Shader/VirtualSurfaceBuffer.cpp
    Shader/ShaderService.cpp
    Build/BuildService.cpp
    Build/CMakeBuildSystem.cpp
    Build/CargoBuildSystem.cpp
    Build/MavenBuildSystem.cpp
    Build/GradleBuildSystem.cpp
    Build/MakeBuildSystem.cpp
    Build/FreePascalBuildSystem.cpp
    Execution/ExecutionService.cpp
    Output/OutputLogManager.cpp
    Output/BuildOutput.cpp
    Output/OutputService.cpp
    Process/ProcessRunner.cpp
    Project/ProjectDetector.cpp
    Project/ProjectService.cpp
    Toolchain/CppToolchain.cpp
    Toolchain/RustToolchain.cpp
    Toolchain/PythonToolchain.cpp
    Toolchain/JavaToolchain.cpp
    Toolchain/FreePascalToolchain.cpp
    Toolchain/ToolchainDetector.cpp
    Toolchain/ToolchainManager.cpp
    Run/RunConfigurationStore.cpp
    Run/RunConfigurationResolver.cpp
    Run/RunService.cpp
)
add_library(Zenvra::Services ALIAS ZDEServices)

target_include_directories(ZDEServices PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(ZDEServices PUBLIC Zenvra::Core Zenvra::UI Zenvra::Tools Zenvra::Graphics)
zde_enable_warnings(ZDEServices)
