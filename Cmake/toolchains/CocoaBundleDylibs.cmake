# Copies every runtime dylib from the bundle's parent directory (the build
# output dir) into the .app's Contents/MacOS so:
#   1. main.cpp's runtime manifest validation (dependencies next to the
#      executable) passes in Release builds too.
#   2. The app stays launchable from Finder (dyld's rpath resolves the same
#      directory implicitly for @rpath references).
# Invoked at build time with cmake -P, so SOURCE_DIR/DEST_DIR are fixed before
# the files are globbed.

file(GLOB DYLIB_FILES "${SOURCE_DIR}/*.dylib")
foreach(dylib IN LISTS DYLIB_FILES)
    file(COPY "${dylib}" DESTINATION "${DEST_DIR}")
endforeach()