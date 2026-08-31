# CMakeLists.txt for Source/Media subsystem.

add_library(ZDEMedia INTERFACE)
add_library(Zenvra::Media ALIAS ZDEMedia)

target_include_directories(ZDEMedia INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
