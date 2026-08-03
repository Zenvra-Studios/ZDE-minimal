add_library(ZDEApplication STATIC
    Application.cpp
    ViewModels/StudioViewModel.cpp
)
add_library(Zenvra::Application ALIAS ZDEApplication)

target_include_directories(ZDEApplication PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ZDEApplication
    PUBLIC Zenvra::Commands Zenvra::Core
    PRIVATE Zenvra::Platform
)
zde_enable_warnings(ZDEApplication)
