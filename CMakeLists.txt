cmake_minimum_required(VERSION 3.0)

project(sylar CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -Wall -ggdb -O0")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(ENABLE_TEST "enable all tests" ON)

message(STATUS "CMake_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMake current system name: ${CMAKE_SYSTEM_NAME}")

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  message(STATUS "Current C Compiler ID: ${CMAKE_C_COMPILER_ID}")
  message(STATUS "Current CXX Compiler ID: ${CMAKE_CXX_COMPILER_ID}")

  if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "The Complie flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}")
  elseif(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "The Complie flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
  endif()

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/${CMAKE_BUILD_TYPE})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/${CMAKE_BUILD_TYPE})
  message(STATUS "Runtime output DIR: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  message(STATUS "Library output DIR: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
  message(STATUS "Archive output DIR: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")

  add_subdirectory(${CMAKE_SOURCE_DIR}/sylar)
endif()

