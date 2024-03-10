#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rome::protos" for configuration "Release"
set_property(TARGET rome::protos APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rome::protos PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libprotos.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS rome::protos )
list(APPEND _IMPORT_CHECK_FILES_FOR_rome::protos "${_IMPORT_PREFIX}/lib/libprotos.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
