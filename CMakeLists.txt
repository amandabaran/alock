cmake_minimum_required(VERSION 3.18)
project(alock LANGUAGES CXX VERSION 0.0.0)

# Options
option(DOCS "Set to build documentation" OFF)

# Flags
set(CXX_STANDARD 20 CACHE STRING "Uses C++20 or 23")

set(LOCK "RdmaSpinLock") #default

#######################################################################################################################
# Setup
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "Using standard ${CXX_STANDARD}")

set(CMAKE_CXX_STANDARD ${CXX_STANDARD})

enable_testing()

#######################################################################################################################
## Libraries

find_package(remus REQUIRED)

#######################################################################################################################
## Install

# Include directories for your current project

add_executable(main main.cc)
target_include_directories(main PRIVATE include)
target_link_libraries(main PUBLIC remus::rdma remus::workload remus::util)
# Set up the logging level
target_compile_definitions(main PUBLIC REMUS_LOG_LEVEL=${LOG_LEVEL})
target_compile_definitions(main PUBLIC LOCK_TYPE=${LOCK} LOCK_HANDLE=${LOCK}Handle)

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
