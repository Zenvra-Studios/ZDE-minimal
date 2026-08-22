# ==============================================================================
# Cmake/Packaging.cmake - Linux Packaging Setup (DEB, RPM, TGZ/TXZ, Arch, Uninstall)
# ==============================================================================

if(APPLE OR WIN32)
    return()
endif()

include(GNUInstallDirs)

# ------------------------------------------------------------------------------
# 1. Target Installation Rules (Binaries & Shared Libraries)
# ------------------------------------------------------------------------------
set(ZDE_PACKAGE_TARGETS
    ZDE
    ZDEApplication
    ZDECommands
    ZDEUI
    ZDETerminal
    ZDEServices
    ZDELanguage
    ZDEPlatform
    ZDEPlatformHost
    ZDEPlatformX11
    BootstrapperLib
    ZDETools
    ZDEUtility
    Plugins
    ThirdParty
    Config
    Scripts
    Graphics
    Audio
    lunasvg
    plutovg
)

foreach(target_name IN LISTS ZDE_PACKAGE_TARGETS)
    if(TARGET ${target_name})
        get_target_property(target_type ${target_name} TYPE)
        if(target_type STREQUAL "EXECUTABLE")
            install(TARGETS ${target_name}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            )
        elseif(target_type STREQUAL "SHARED_LIBRARY" OR target_type STREQUAL "MODULE_LIBRARY")
            install(TARGETS ${target_name}
                LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/zde"
                RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            )
        endif()
    endif()
endforeach()

# Configure RPATH for installed executable
if(TARGET ZDE)
    set_target_properties(ZDE PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/zde:$ORIGIN/../lib/zde:$ORIGIN/../lib:$ORIGIN"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
endif()

# ------------------------------------------------------------------------------
# 2. Desktop Entry & Icon Installation
# ------------------------------------------------------------------------------

# Desktop file
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/packaging/zde.desktop")
    install(
        FILES "${CMAKE_CURRENT_LIST_DIR}/packaging/zde.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
elseif(EXISTS "${CMAKE_SOURCE_DIR}/ZDE.desktop")
    install(
        FILES "${CMAKE_SOURCE_DIR}/ZDE.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        RENAME "zde.desktop"
    )
endif()

# Standard hicolor application icon
if(EXISTS "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_512x512.png")
    install(
        FILES "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_512x512.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
        RENAME "zde.png"
    )
    install(
        FILES "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_512x512.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps"
        RENAME "zde.png"
    )
elseif(EXISTS "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_build.png")
    install(
        FILES "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_build.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
        RENAME "zde.png"
    )
    install(
        FILES "${CMAKE_SOURCE_DIR}/Assets/icons/zenvra_logo_build.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps"
        RENAME "zde.png"
    )
endif()

# ------------------------------------------------------------------------------
# 3. Assets & Manifest Installation
# ------------------------------------------------------------------------------

if(EXISTS "${CMAKE_SOURCE_DIR}/Assets")
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/Assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zde/Assets"
    )
    # Also install as Resources directory next to datadir
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/Assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zde/Resources"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/manifest")
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/manifest/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zde/manifest"
    )
endif()

# ------------------------------------------------------------------------------
# 4. Uninstall Target (Pure CMake Uninstall via install_manifest.txt)
# ------------------------------------------------------------------------------

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Uninstalling ZDE from system..."
    )
endif()

# ------------------------------------------------------------------------------
# 5. CPack Packaging Configuration
# ------------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "zde")
set(CPACK_PACKAGE_VENDOR "Zenvra Studios")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Zenvra Development Editor - Lightweight C++ IDE")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/zenvra-studios/ZDE-minimal")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "support@zenvra.com")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

# --- Debian / Ubuntu Package (.deb) ---
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Zenvra Studios <support@zenvra.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libxft2, libfontconfig1, libxext6")

# --- Fedora / RHEL / openSUSE Package (.rpm) ---
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_PACKAGE_REQUIRES "libX11, libXft, fontconfig, libXext")

# --- Archive Generators (.tar.gz, .tar.xz) ---
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

include(CPack)

# ------------------------------------------------------------------------------
# 6. Convenience Pure CMake Packaging Targets
# ------------------------------------------------------------------------------

if(NOT TARGET package_deb)
    add_custom_target(package_deb
        COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating Debian/Ubuntu .deb package..."
    )
endif()

if(NOT TARGET package_rpm)
    add_custom_target(package_rpm
        COMMAND ${CMAKE_CPACK_COMMAND} -G RPM
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating Fedora/RHEL .rpm package..."
    )
endif()

if(NOT TARGET package_arch)
    add_custom_target(package_arch
        COMMAND makepkg -f
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/packaging/arch"
        COMMENT "Generating Arch Linux .pkg.tar.zst package..."
    )
endif()
