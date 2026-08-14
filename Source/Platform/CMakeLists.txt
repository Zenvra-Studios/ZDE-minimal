if(WIN32)
    add_subdirectory(Win32)
elseif(APPLE)
    add_subdirectory(Cocoa)
elseif(UNIX)
    add_subdirectory(X11)
endif()

add_library(ZDEPlatform
    PlatformWindowFactory.cpp
)
add_library(Zenvra::Platform ALIAS ZDEPlatform)

target_include_directories(ZDEPlatform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." "${PROJECT_SOURCE_DIR}")
target_link_libraries(ZDEPlatform PUBLIC Zenvra::Core)

if(WIN32)
    target_link_libraries(ZDEPlatform PRIVATE ZDEPlatformWin32)
elseif(APPLE)
    target_link_libraries(ZDEPlatform PRIVATE ZDEPlatformCocoa)
elseif(UNIX)
    target_link_libraries(ZDEPlatform PRIVATE ZDEPlatformX11)
endif()

zde_enable_warnings(ZDEPlatform)
