add_library(BootstrapperLib 
    ManifestParser.cpp
)

target_include_directories(BootstrapperLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(BootstrapperLib PRIVATE ZDE_BOOTSTRAPPER_BUILD)

if(WIN32)
    target_sources(BootstrapperLib PRIVATE
        Win32/ProcessLauncherWin32.cpp
        Win32/ValidatorWin32.cpp
        Win32/Win32UI.cpp
    )
    target_link_libraries(BootstrapperLib PRIVATE user32)
elseif(APPLE)
    target_sources(BootstrapperLib PRIVATE
        Cocoa/ProcessLauncherCocoa.mm
        Cocoa/ValidatorCocoa.mm
        Cocoa/CocoaUI.mm
    )
else()
    target_sources(BootstrapperLib PRIVATE
        X11/ProcessLauncherX11.cpp
        X11/ValidatorX11.cpp
        X11/X11UI.cpp
    )
endif()

zde_enable_warnings(BootstrapperLib)
