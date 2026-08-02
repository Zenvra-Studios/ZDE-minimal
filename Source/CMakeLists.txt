add_executable(ZDE main.cpp)

# Every product module subdirectory that has its own CMakeLists.txt is added.
# Each module only creates its static library when it actually contains
# sources, so empty modules are safe no-ops until they are implemented.
set(ZDE_MODULES
    AI
    Application
    Commands
    Core
    Debugger
    Editors
    Explorer
    Language
    Panels
    Plugins
    Server
    Services
    Terminal
    UI
    Utility
    Workspace
)

foreach(module_dir IN LISTS ZDE_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module_dir}/CMakeLists.txt")
        add_subdirectory("${module_dir}")
    endif()
endforeach()

# Link every module library that actually exists into the application, so
# module headers and code interconnect with the executable (like an app
# that ships its runtime libraries and links against them).
foreach(module_dir IN LISTS ZDE_MODULES)
    if(TARGET "${module_dir}")
        target_link_libraries(ZDE PRIVATE "${module_dir}")
    endif()
endforeach()

# Add every platform subdirectory that has its own CMakeLists.txt.
# Each platform file guards itself (returns early when not on its OS),
# so all three can be added on every host without breaking the build.
foreach(platform_dir IN ITEMS Win32 Cocoa X11)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Platform/${platform_dir}/CMakeLists.txt")
        add_subdirectory("Platform/${platform_dir}")
    endif()
endforeach()

# All platform libraries exist, but only the active one is linked.
if(WIN32)
    target_link_libraries(ZDE PRIVATE PlatformWin32)
    # Resources (IDR_MAINMENU etc.) must be compiled into the executable,
    # resources inside static libraries are discarded at link time.
    target_sources(ZDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Win32/Config/Win32Resources.rc")
elseif(APPLE)
    target_link_libraries(ZDE PRIVATE PlatformCocoa)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(ZDE PRIVATE PlatformX11)
endif()
