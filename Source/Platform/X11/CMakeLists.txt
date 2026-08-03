if(NOT UNIX OR APPLE)
    return()
endif()

find_package(X11 REQUIRED)

add_library(ZDEPlatformX11 STATIC
    X11Window.cpp
    Components/X11ChromeRenderer.cpp
    Runtime/X11Context.cpp
)

target_include_directories(ZDEPlatformX11 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(ZDEPlatformX11
    PUBLIC Zenvra::Core X11::X11
    PRIVATE Zenvra::UI
)
zde_enable_warnings(ZDEPlatformX11)
