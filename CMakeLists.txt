# SPDX-License-Identifier: GPL-2.0-or-later
#
# CMake build of EUSBASPI's native USB stack (C, for OpenASPI's native
# layer in 32/64-bit kernels such as GonWare's GDOS). The DOS manager keeps
# its Makefile.
#
# Needs the OpenASPI::native target: a parent project provides it, or
# OPENASPI_SOURCE_DIR points to an OpenASPI checkout (default: ../OpenAspi
# or ../OpenASPI next to this one).

cmake_minimum_required(VERSION 3.20)
project(EUSBASPI LANGUAGES C)

if(NOT TARGET OpenASPI::native)
  set(OPENASPI_SOURCE_DIR "" CACHE PATH "OpenASPI source tree")
  if(NOT OPENASPI_SOURCE_DIR)
    foreach(_d OpenAspi OpenASPI)
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${_d}/CMakeLists.txt")
        set(OPENASPI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${_d}")
      endif()
    endforeach()
  endif()
  if(NOT OPENASPI_SOURCE_DIR)
    message(FATAL_ERROR "EUSBASPI: set OPENASPI_SOURCE_DIR to an OpenASPI checkout")
  endif()
  add_subdirectory("${OPENASPI_SOURCE_DIR}" openaspi EXCLUDE_FROM_ALL)
endif()

# The USB stack: the parent's EUSBCORE::core, else the submodule
# third_party/eusbcore or EUSBCORE_SOURCE_DIR (default: ../eusbcore).
if(NOT TARGET EUSBCORE::core)
  set(EUSBCORE_SOURCE_DIR "" CACHE PATH "eusbcore source tree")
  if(NOT EUSBCORE_SOURCE_DIR)
    foreach(_d "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eusbcore" "${CMAKE_CURRENT_SOURCE_DIR}/../eusbcore")
      if(NOT EUSBCORE_SOURCE_DIR AND EXISTS "${_d}/CMakeLists.txt")
        set(EUSBCORE_SOURCE_DIR "${_d}")
      endif()
    endforeach()
  endif()
  if(NOT EUSBCORE_SOURCE_DIR)
    message(FATAL_ERROR "EUSBASPI: eusbcore not found (git submodule update --init)")
  endif()
  add_subdirectory("${EUSBCORE_SOURCE_DIR}" eusbcore EXCLUDE_FROM_ALL)
endif()

add_library(eusbaspi_native STATIC native/msc.c)
target_include_directories(eusbaspi_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(eusbaspi_native PUBLIC EUSBCORE::core OpenASPI::native)
set_target_properties(eusbaspi_native PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_library(EUSBASPI::native ALIAS eusbaspi_native)

if(PROJECT_IS_TOP_LEVEL)
  target_compile_options(eusbaspi_native PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
