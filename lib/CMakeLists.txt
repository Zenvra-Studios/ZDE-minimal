# lib/CMakeLists.txt
# Handles deployment / linking of shared libraries from lib/shared to the runtime binary directory (e.g. bin/Debug)

file(GLOB SHARED_LIBRARIES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.so*"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.dylib"
)

add_custom_target(zde_shared_libs ALL
    COMMENT "Deploying shared libraries from lib/shared to runtime output directory..."
)

set_target_properties(zde_shared_libs PROPERTIES FOLDER "Libraries")

foreach(lib_file IN LISTS SHARED_LIBRARIES)
    get_filename_component(lib_name "${lib_file}" NAME)
    add_custom_command(
        TARGET zde_shared_libs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${lib_file}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${lib_name}"
        COMMENT "Deploying shared library: ${lib_name} -> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endforeach()
