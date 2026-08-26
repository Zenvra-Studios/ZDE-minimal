add_library(ZDETerminal
    TerminalExitDecoder.cpp
    TerminalPanelModel.cpp
    TerminalResizeModel.cpp
    TerminalSession.cpp
    WindowsSecurityProbe.cpp
)
add_library(Zenvra::Terminal ALIAS ZDETerminal)

target_include_directories(ZDETerminal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(UNIX AND NOT APPLE)
    target_link_libraries(ZDETerminal PRIVATE util)
endif()
zde_enable_warnings(ZDETerminal)
