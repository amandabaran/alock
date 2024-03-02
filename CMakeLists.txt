cmake_minimum_required(VERSION 3.18)
project(alock LANGUAGES CXX VERSION 0.0.0)

# Options
option(DOCS "Set to build documentation" OFF)

# Flags
set(LOG_LEVEL "INFO" CACHE STRING "Log level options include TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, and OFF")
set(CXX_STANDARD 20 CACHE STRING "Uses C++20 or 23")

#######################################################################################################################
# Setup

set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)

message(STATUS "Using standard ${CXX_STANDARD}")

set(CMAKE_CXX_STANDARD ${CXX_STANDARD})

# Ensure the LOG_LEVEL is correct
set(LOG_LEVELS "TRACE;DEBUG;INFO;WARN;ERROR;CRITICAL;OFF")
foreach(L ${LOG_LEVELS})
  if("${LOG_LEVEL}" STREQUAL "${L}")
    set(CORRECT_LOG_LEVEL TRUE)
  endif()
endforeach()
if(NOT DEFINED CORRECT_LOG_LEVEL)
  message(FATAL_ERROR "LOG_LEVEL must be one of ${LOG_LEVELS}") 
endif()
message(STATUS "Using LOG_LEVEL=${LOG_LEVEL}")

if(DOCS)
    find_package(Doxygen REQUIRED)  # use doxygen for building docs
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating docs through doxygen")    
endif()

# Use a custom rule (in `cmake/`) to find the ibverbs and rdmacm libraries.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(RDMA REQUIRED)

find_package(GTest 1.11 REQUIRED) #defines GTest::gtest
message(STATUS "Using gtest version ${GTest_VERSION}")

find_package(absl 20210324 REQUIRED) # defines absl::absl
message(STATUS "Using absl version ${absl_VERSION}")

find_package(Protobuf REQUIRED)
message(STATUS "Using protobuf version ${Protobuf_VERSION}")

find_package(fmt 8.1...<8.2 REQUIRED) # defines fmt::fmt

message(STATUS "Using fmt version ${fmt_VERSION}")

find_package(spdlog 1.9...<1.10 REQUIRED) #defines spdlog::spdlog
message(STATUS "Using spdlog version ${spdlog_VERSION}")

find_package(Coroutines REQUIRED) # defines std::coroutines for linking

find_package(RDMA REQUIRED) # defines rdma::ibverbs and rdma::cm for linking

#######################################################################################################################
## Libraries

enable_testing()

add_subdirectory(protos) 

add_subdirectory(tests)


#add_library(spdlog_vendor INTERFACE) # TODO get rid of this requirement
#target_include_directories(spdlog_vendor INTERFACE 
#                           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/vendor/spdlog-1.12.0>
#                           $<INSTALL_INTERFACE:include>)
#install(DIRECTORY ${PROJECT_SOURCE_DIR}/vendor/spdlog-1.12.0 DESTINATION include) 

add_library(sss INTERFACE)
target_include_directories(sss INTERFACE 
                           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/vendor/sss>
                           $<INSTALL_INTERFACE:include>)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/vendor/sss/sss DESTINATION include) 

## rome::workload
add_subdirectory(workload)

## rome::logging
add_subdirectory(logging)

## rome::metrics
add_subdirectory(metrics)

## rome::rdma
add_subdirectory(rdma)

## rome::util
add_subdirectory(one_lock)


#######################################################################################################################
## Install

install(TARGETS protos 
                EXPORT alockTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include)

# install the include dirs
install(FILES ${PROJECT_SOURCE_DIR}/protos/experiment.proto DESTINATION protos)
install(FILES ${PROJECT_SOURCE_DIR}/protos/rdma.proto DESTINATION protos)
install(FILES ${PROJECT_SOURCE_DIR}/protos/metrics.proto DESTINATION protos)
install(FILES ${PROJECT_SOURCE_DIR}/protos/workloaddriver.proto DESTINATION protos)
# install protos for reuse

install(EXPORT alockTargets
        FILE alockTargets.cmake
        NAMESPACE rome::
        DESTINATION lib/cmake)

install(FILES ${PROJECT_SOURCE_DIR}/share/romeConfig.cmake DESTINATION lib/cmake)

install(FILES ${PROJECT_SOURCE_DIR}/cmake/FindRDMA.cmake DESTINATION lib/cmake)
install(FILES ${PROJECT_SOURCE_DIR}/cmake/FindCoroutines.cmake DESTINATION lib/cmake)

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
## Examples

enable_testing()
add_subdirectory(benchmark/one_lock)