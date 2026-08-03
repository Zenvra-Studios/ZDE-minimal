add_subdirectory(Core)
add_subdirectory(Commands)
add_subdirectory(UI)
add_subdirectory(Terminal)
add_subdirectory(Platform)
add_subdirectory(Application)

add_executable(ZDE main.cpp)
target_link_libraries(ZDE PRIVATE Zenvra::Application)
target_include_directories(ZDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
zde_enable_warnings(ZDE)

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
    Services
    Utility
    Workspace
)
    add_subdirectory(${module_dir})
endforeach()
