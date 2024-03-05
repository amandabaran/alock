cmake_minimum_required(VERSION 3.18)
project(alock LANGUAGES CXX VERSION 0.0.0)

# Options
option(DOCS "Set to build documentation" OFF)

# Flags
set(CXX_STANDARD 20 CACHE STRING "Uses C++20 or 23")

#######################################################################################################################
# Setup
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "Using standard ${CXX_STANDARD}")

set(CMAKE_CXX_STANDARD ${CXX_STANDARD})

enable_testing()

#######################################################################################################################
## Libraries

add_subdirectory(../remus remus)

add_subdirectory(protos) #TODO get rid of this requirement

add_subdirectory(src)

add_subdirectory(tests)

#######################################################################################################################
## Install

install(TARGETS src
                EXPORT alockTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include)

# install the include dirs
install(FILES ${PROJECT_SOURCE_DIR}/protos/experiment.proto DESTINATION protos)
install(FILES ${PROJECT_SOURCE_DIR}/protos/cluster.proto DESTINATION protos)
# install protos for reuse

#######################################################################################################################
## DEB Package

set(CPACK_PACKAGE_VENDOR "Scalable Systems and Software Lab")
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Scalable Systems and Software Lab")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
        /.git
        /dist
        /.*build.*
        /\\\\.DS_Store
        )
include(CPack)

#######################################################################################################################
