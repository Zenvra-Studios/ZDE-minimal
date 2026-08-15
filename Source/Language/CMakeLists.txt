# CMakeLists.txt for Language module.
file(GLOB_RECURSE Language_SOURCES CONFIGURE_DEPENDS
    "*.h" "*.hpp" "*.c" "*.cpp" "*.cc"
)

add_library(ZDELanguage ${Language_SOURCES})
add_library(Zenvra::Language ALIAS ZDELanguage)
add_library(Language ALIAS ZDELanguage)

target_include_directories(ZDELanguage PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/Source"
)

target_link_libraries(ZDELanguage
    PUBLIC Zenvra::Core nlohmann_json::nlohmann_json
)

zde_enable_warnings(ZDELanguage)
