#.rst:
# FindFFmpeg
# ----------
#
# Find the native FFmpeg includes and libraries.
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# An interface target ``FFmpeg::FFmpeg`` is defined that links all requested
# FFmpeg components.
#
# Additionally, individual imported targets are defined for each found component:
#   - ``FFmpeg::avcodec``
#   - ``FFmpeg::avformat``
#   - ``FFmpeg::avutil``
#   - ``FFmpeg::swscale``
#   - ``FFmpeg::swresample``
#   - ``FFmpeg::avfilter``
#   - ``FFmpeg::avdevice``
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This will define the following variables:
#
# ``FFMPEG_FOUND``
#   True if FFmpeg was found.
# ``FFMPEG_INCLUDE_DIRS``
#   All include directories for found components.
# ``FFMPEG_LIBRARIES``
#   All libraries for found components.
# ``FFMPEG_VERSION``
#   The version of FFmpeg found.
#

include(FindPackageHandleStandardArgs)

set(_FFMPEG_SEARCH_PATHS
    ${FFMPEG_ROOT}
    $ENV{FFMPEG_ROOT}
    ${FFMPEG_DIR}
    $ENV{FFMPEG_DIR}
    "${CMAKE_SOURCE_DIR}/ThirdParty/ffmpeg"
    "${CMAKE_SOURCE_DIR}/ThirdParty/FFmpeg"
    "c:/Users/Administrator/Documents/ZDE-minimal/ThirdParty/ffmpeg"
    "C:/ThirdParty/ffmpeg"
    "C:/ThirdParty/FFmpeg"
    "${CMAKE_SOURCE_DIR}/lib/ffmpeg"
    /usr/local
    /usr
    /opt/local
    /opt/homebrew
)

# Macro to find an individual component
macro(_FFMPEG_FIND_COMPONENT _comp _header _libname)
    string(TOUPPER "${_comp}" _upper_comp)

    find_path(FFMPEG_${_upper_comp}_INCLUDE_DIR
        NAMES ${_header}
        HINTS ${_FFMPEG_SEARCH_PATHS}
        PATH_SUFFIXES include
    )

    find_library(FFMPEG_${_upper_comp}_LIBRARY
        NAMES ${_libname} lib${_libname} ${_comp} lib${_comp}
        HINTS ${_FFMPEG_SEARCH_PATHS}
        PATH_SUFFIXES lib bin lib64
    )

    if(FFMPEG_${_upper_comp}_INCLUDE_DIR AND FFMPEG_${_upper_comp}_LIBRARY)
        set(FFMPEG_${_comp}_FOUND TRUE)
        set(FFMPEG_${_upper_comp}_FOUND TRUE)

        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${_upper_comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${_upper_comp}_INCLUDE_DIR}"
            )
        endif()

        list(APPEND FFMPEG_INCLUDE_DIRS "${FFMPEG_${_upper_comp}_INCLUDE_DIR}")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${_upper_comp}_LIBRARY}")
    else()
        set(FFMPEG_${_comp}_FOUND FALSE)
        set(FFMPEG_${_upper_comp}_FOUND FALSE)
    endif()

    mark_as_advanced(FFMPEG_${_upper_comp}_INCLUDE_DIR FFMPEG_${_upper_comp}_LIBRARY)
endmacro()

# Check requested components or default to core set
if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS avcodec avformat avutil swscale swresample)
endif()

# Find components
foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    if(_comp STREQUAL "avcodec")
        _FFMPEG_FIND_COMPONENT(avcodec "libavcodec/avcodec.h" "avcodec")
    elseif(_comp STREQUAL "avformat")
        _FFMPEG_FIND_COMPONENT(avformat "libavformat/avformat.h" "avformat")
    elseif(_comp STREQUAL "avutil")
        _FFMPEG_FIND_COMPONENT(avutil "libavutil/avutil.h" "avutil")
    elseif(_comp STREQUAL "swscale")
        _FFMPEG_FIND_COMPONENT(swscale "libswscale/swscale.h" "swscale")
    elseif(_comp STREQUAL "swresample")
        _FFMPEG_FIND_COMPONENT(swresample "libswresample/swresample.h" "swresample")
    elseif(_comp STREQUAL "avfilter")
        _FFMPEG_FIND_COMPONENT(avfilter "libavfilter/avfilter.h" "avfilter")
    elseif(_comp STREQUAL "avdevice")
        _FFMPEG_FIND_COMPONENT(avdevice "libavdevice/avdevice.h" "avdevice")
    else()
        message(WARNING "Unknown FFmpeg component: ${_comp}")
    endif()
endforeach()

if(FFMPEG_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
endif()

# Extract version from libavutil if found
if(FFMPEG_AVUTIL_INCLUDE_DIR AND EXISTS "${FFMPEG_AVUTIL_INCLUDE_DIR}/libavutil/version.h")
    file(READ "${FFMPEG_AVUTIL_INCLUDE_DIR}/libavutil/version.h" _ver_header)
    string(REGEX MATCH "#define[ \t]+LIBAVUTIL_VERSION_MAJOR[ \t]+([0-9]+)" _match "${_ver_header}")
    set(_major "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+LIBAVUTIL_VERSION_MINOR[ \t]+([0-9]+)" _match "${_ver_header}")
    set(_minor "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+LIBAVUTIL_VERSION_MICRO[ \t]+([0-9]+)" _match "${_ver_header}")
    set(_micro "${CMAKE_MATCH_1}")
    if(_major AND _minor AND _micro)
        set(FFMPEG_VERSION "${_major}.${_minor}.${_micro}")
    endif()
endif()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    HANDLE_COMPONENTS
    VERSION_VAR FFMPEG_VERSION
)

# Umbrella target
if(FFMPEG_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
endif()
