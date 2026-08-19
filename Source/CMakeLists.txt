add_subdirectory(Core)
add_subdirectory(Commands)
# add_subdirectory(SourceControl)
add_subdirectory(UI)
add_subdirectory(Terminal)
add_subdirectory(Tools)
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
    set_target_properties(ZDE PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Cocoa/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "ZDE"
        MACOSX_BUNDLE_DISPLAY_NAME "ZDE"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.zenvra.zde"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )

    # 1. Copy AppIcon.icns into bundle Resources
    if(EXISTS "${CMAKE_SOURCE_DIR}/Assets/icons/AppIcon.icns")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Assets/icons/AppIcon.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/Resources/AppIcon.icns"
            COMMENT "Deploying AppIcon.icns to .app Resources..."
        )
    endif()

    # 2. Copy entire Assets tree (fonts, icons, Grammars) to Resources/Assets and MacOS/Assets
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/Resources/Assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets"
            "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/Resources/Assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets"
            "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/MacOS/Assets"
        COMMENT "Deploying all Assets (icons, fonts, grammars) to macOS bundle..."
    )

    # 3. Deploy manifest into MacOS/manifest
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/manifest"
            "$<TARGET_BUNDLE_CONTENT_DIR:ZDE>/MacOS/manifest"
        COMMENT "Deploying runtime manifest to macOS bundle..."
    )

    # 4. Bundle the runtime dylibs themselves
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=$<TARGET_FILE_DIR:ZDE>/../../.."
            "-DDEST_DIR=$<TARGET_FILE_DIR:ZDE>"
            -P "${CMAKE_SOURCE_DIR}/Cmake/toolchains/CocoaBundleDylibs.cmake"
        COMMENT "Bundling runtime dylibs into macOS bundle..."
    )
endif()

if(WIN32)
    target_sources(ZDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Win32/Config/Win32Resources.rc")

    # Deploy clangd binary strictly inside plugins/lsp/ folder only (no root duplicates)
    set(CLANGD_SRC "${CMAKE_SOURCE_DIR}/ThirdParty/clangd-windows-22.1.6/clangd.exe")
    if(EXISTS "${CLANGD_SRC}")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/plugins/lsp"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CLANGD_SRC}" "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/clangd.exe"
            COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:ZDE>/clangd.exe"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:ZDE>/plugin"
            COMMENT "Deploying clangd binary cleanly into plugins/lsp/..."
        )
    endif()

    # Deploy cmake-ls binary strictly inside plugins/lsp/ folder (cmakels.exe only)
    set(CMAKELS_SRC "${CMAKE_SOURCE_DIR}/ThirdParty/cmake-ls/cmakels-win64.exe")
    if(EXISTS "${CMAKELS_SRC}")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/plugins/lsp"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKELS_SRC}" "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/cmakels.exe"
            COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/cmakels-win64.exe"
            COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/cmake-language-server.exe"
            COMMENT "Deploying cmakels binary cleanly into plugins/lsp/..."
        )
    endif()

    # Deploy rust-analyzer binary strictly inside plugins/lsp/ folder
    set(RUST_ANALYZER_SRC "${CMAKE_SOURCE_DIR}/ThirdParty/rust-analyzer-x86_64-pc-windows-msvc/rust-analyzer.exe")
    if(EXISTS "${RUST_ANALYZER_SRC}")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/plugins/lsp"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RUST_ANALYZER_SRC}" "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/rust-analyzer.exe"
            COMMENT "Deploying rust-analyzer binary cleanly into plugins/lsp/..."
        )
    endif()

    # Deploy typescript-language-server binary strictly inside plugins/lsp/ folder
    set(TLS_SRC "${CMAKE_SOURCE_DIR}/ThirdParty/tls/typescript-language-server.exe")
    if(EXISTS "${TLS_SRC}")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/plugins/lsp"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TLS_SRC}" "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/typescript-language-server.exe"
            COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/tls.exe"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:ZDE>/plugins/lsp/tls"
            COMMENT "Deploying typescript-language-server binary cleanly into plugins/lsp/..."
        )
    endif()

    # Deploy Resources directory strictly containing icons and fonts only
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets/icons"
            "$<TARGET_FILE_DIR:ZDE>/Resources/icons"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets/fonts"
            "$<TARGET_FILE_DIR:ZDE>/Resources/fonts"
        COMMENT "Deploying Resources folder (icons and fonts only)..."
    )

    # Deploy TextMate Grammars to Assets/Grammars
    if(EXISTS "${CMAKE_SOURCE_DIR}/Assets/grammars")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets/grammars"
                "$<TARGET_FILE_DIR:ZDE>/Assets/grammars"
            COMMENT "Deploying TextMate Grammars to Assets/grammars..."
        )
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/Assets/Grammars")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets/Grammars"
                "$<TARGET_FILE_DIR:ZDE>/Assets/Grammars"
            COMMENT "Deploying TextMate Grammars to Assets/Grammars..."
        )
    endif()
endif()

if(NOT WIN32)
    add_custom_command(TARGET ZDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZDE>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets/icons"
            "$<TARGET_FILE_DIR:ZDE>/Resources/icons"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Assets/fonts"
            "$<TARGET_FILE_DIR:ZDE>/Resources/fonts"
        COMMENT "Deploying Resources folder (icons and fonts only)..."
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/Assets/grammars")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets/grammars"
                "$<TARGET_FILE_DIR:ZDE>/Assets/grammars"
            COMMENT "Deploying TextMate Grammars to Assets/grammars..."
        )
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/Assets/Grammars")
        add_custom_command(TARGET ZDE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets/Grammars"
                "$<TARGET_FILE_DIR:ZDE>/Assets/Grammars"
            COMMENT "Deploying TextMate Grammars to Assets/Grammars..."
        )
    endif()
endif()

# These folders are reserved module boundaries from the product roadmap.
# Their current CMake files remain safe no-ops until implementation starts.
foreach(module_dir IN ITEMS
    AI
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
