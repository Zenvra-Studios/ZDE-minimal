add_library(ZDESourceControl
    GitRepository.cpp
    GitFlow.cpp
)
add_library(Zenvra::SourceControl ALIAS ZDESourceControl)

target_include_directories(ZDESourceControl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
    "${libgit2_SOURCE_DIR}/include"
)

# libgit2 creates either 'git2' or 'libgit2package' target
if(TARGET libgit2package)
    target_link_libraries(ZDESourceControl PUBLIC libgit2package)
elseif(TARGET git2)
    target_link_libraries(ZDESourceControl PUBLIC git2)
elseif(TARGET libgit2)
    target_link_libraries(ZDESourceControl PUBLIC libgit2)
endif()

target_link_libraries(ZDESourceControl PUBLIC Zenvra::Core)

if(APPLE)
    target_link_libraries(ZDESourceControl PRIVATE "-framework Security" "-framework CoreFoundation" iconv)
elseif(WIN32)
    target_link_libraries(ZDESourceControl PRIVATE winhttp crypt32 rpcrt4)
endif()

zde_enable_warnings(ZDESourceControl)
