add_subdirectory(Core)
add_subdirectory(Commands)
add_subdirectory(UI)
add_subdirectory(Terminal)
add_subdirectory(Services)
add_subdirectory(Platform)
add_subdirectory(Application)
add_subdirectory(Bootstrapper)

if(APPLE)
    add_executable(ZDE MACOSX_BUNDLE main.cpp)
else()
    add_executable(ZDE main.cpp)
endif()
target_link_libraries(ZDE PRIVATE Zenvra::Application BootstrapperLib)
target_include_directories(ZDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
zde_enable_warnings(ZDE)

# Deploy the runtime manifest next to the executable. This is a standalone
# stamp-driven rule (not a POST_BUILD of ZDE) so editing manifest/runtime.json
# alone re-copies it on the next build even when ZDE itself has nothing to
# relink. POST_BUILD commands only run when the target relinks, which is why
# the manifest previously went stale.
if(APPLE)
    set(ZDE_MANIFEST_DEST "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/MacOS/manifest")
else()
    set(ZDE_MANIFEST_DEST "$<TARGET_FILE_DIR:ZDE>/manifest")
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-runtime.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/manifest"
        "${ZDE_MANIFEST_DEST}"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/manifest-runtime.stamp"
    DEPENDS "${CMAKE_SOURCE_DIR}/manifest/runtime.json"
)
add_custom_target(ZDE_Manifest DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/manifest-runtime.stamp")
add_dependencies(ZDE ZDE_Manifest)

if(APPLE)
    # main.cpp resolves runtime.json relative to the executable directory,
    # which inside a .app is Contents/MacOS. The stamp-driven rule above also
    # copies it; this one just guarantees presence after the bundle link.
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/manifest"
        "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/MacOS/manifest"
    )
    # Bundle the runtime dylibs themselves. The manifest above validates that
    # every dependency sits next to the executable (Contents/MacOS), so copy
    # all *.dylib from the build output dir into the bundle.
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=$<TARGET_FILE_DIR:ZDE>/../../.."
            "-DDEST_DIR=$<TARGET_FILE_DIR:ZDE>"
            -P "${CMAKE_SOURCE_DIR}/Cmake/toolchains/CocoaBundleDylibs.cmake"
    )
endif()

if(WIN32)
    target_sources(ZDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Win32/Config/Win32Resources.rc")
endif()

# These folders are reserved module boundaries from the product roadmap.
# Their current CMake files remain safe no-ops until implementation starts.
foreach(module_dir IN ITEMS
    AI
    Debugger
    Editors
    Explorer
    Language
    Panels
    Plugins
    Server
    Utility
    Workspace
)
    add_subdirectory(${module_dir})
endforeach()
