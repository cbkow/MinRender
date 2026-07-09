#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "PNG::png_shared" for configuration "Release"
set_property(TARGET PNG::png_shared APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(PNG::png_shared PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libpng16.16.44.0.dylib"
  IMPORTED_SONAME_RELEASE "@rpath/libpng16.16.dylib"
  )

list(APPEND _cmake_import_check_targets PNG::png_shared )
list(APPEND _cmake_import_check_files_for_PNG::png_shared "${_IMPORT_PREFIX}/lib/libpng16.16.44.0.dylib" )

# Import target "PNG::png_framework" for configuration "Release"
set_property(TARGET PNG::png_framework APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(PNG::png_framework PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/png.framework/Versions/1.6.44/png"
  IMPORTED_SONAME_RELEASE "@rpath/png.framework/Versions/1.6.44/png"
  )

list(APPEND _cmake_import_check_targets PNG::png_framework )
list(APPEND _cmake_import_check_files_for_PNG::png_framework "${_IMPORT_PREFIX}/lib/png.framework/Versions/1.6.44/png" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
