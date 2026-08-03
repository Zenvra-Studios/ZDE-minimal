if(NOT APPLE)
    return()
endif()

add_library(ZDEPlatformCocoa STATIC
    CocoaWindow.mm
    Runtime/CocoaContext.mm
)

target_include_directories(ZDEPlatformCocoa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(ZDEPlatformCocoa PUBLIC Zenvra::Core PRIVATE "-framework Cocoa")
zde_enable_warnings(ZDEPlatformCocoa)
