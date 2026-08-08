add_library(ZDECommands
    CommandRegistry.cpp
)
add_library(Zenvra::Commands ALIAS ZDECommands)

target_include_directories(ZDECommands PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ZDECommands PUBLIC Zenvra::Core)
zde_enable_warnings(ZDECommands)
