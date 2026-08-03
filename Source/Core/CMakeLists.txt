add_library(ZDECore INTERFACE)
add_library(Zenvra::Core ALIAS ZDECore)

target_include_directories(ZDECore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
