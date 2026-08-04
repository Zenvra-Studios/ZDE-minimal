# CMakeLists.txt for Utility module.

set(Utility_SOURCES
    Column.h
    DPI.h
    DragDropModel.cpp
    DragDropModel.h
    Flex.h
    Fonts.h
    Gradient.h
    Grid.h
    IcoDecoder.h
    Logger.h
    MathUtil.h
    Row.h
    Shadows.h
    stb_image.cpp
    stb_image.h
    TextEncoding.h
    UiHover.h
    X11Rounded.h
)

if(Utility_SOURCES)
    add_library(ZDEUtility STATIC ${Utility_SOURCES})
    set_target_properties(ZDEUtility PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(ZDEUtility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
