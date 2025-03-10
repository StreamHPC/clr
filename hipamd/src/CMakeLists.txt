# Copyright (c) 2020 - 2022 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

cmake_minimum_required(VERSION 3.5.1)

include(GNUInstallDirs)

set(VERSION_MAJOR_AMDHIP ${HIP_VERSION_MAJOR})
set(VERSION_MINOR_AMDHIP ${HIP_VERSION_MINOR})

if(ADDRESS_SANITIZER)
  set(ASAN_LINKER_FLAGS "-fsanitize=address")
  set(ASAN_COMPILER_FLAGS "-fno-omit-frame-pointer -fsanitize=address")

  if(NOT CMAKE_COMPILER_IS_GNUCC)
    if(BUILD_SHARED_LIBS)
      set(ASAN_COMPILER_FLAGS "${ASAN_COMPILER_FLAGS} -shared-libsan")
      set(ASAN_LINKER_FLAGS "${ASAN_LINKER_FLAGS} -shared-libsan")
    else()
      set(ASAN_LINKER_FLAGS "${ASAN_LINKER_FLAGS} -static-libsan")
    endif()
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_COMPILER_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_COMPILER_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ASAN_LINKER_FLAGS} -s -Wl,--build-id=sha1")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ASAN_LINKER_FLAGS} -Wl,--build-id=sha1")
endif()

if(CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=deprecated-declarations")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=deprecated-declarations")
endif()

option(DISABLE_DIRECT_DISPATCH "Disable Direct Dispatch" OFF)

option(BUILD_SHARED_LIBS "Build the shared library" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

if(BUILD_SHARED_LIBS)
  add_library(amdhip64 SHARED)
  # Windows doesn't have a strip utility, so CMAKE_STRIP won't be set.
  if((CMAKE_BUILD_TYPE STREQUAL "Release") AND NOT ("${CMAKE_STRIP}" STREQUAL ""))
    add_custom_command(TARGET amdhip64 POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:amdhip64>)
  endif()
else()
  add_library(amdhip64 STATIC rocclr)
endif()

set_target_properties(amdhip64 PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  # Workaround for many places in the HIP project
  # having hardcoded references to build/lib/libamdhip64.so
  LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
  ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_target_properties(amdhip64 PROPERTIES OUTPUT_NAME "amdhip64")
else()
  set_target_properties(amdhip64 PROPERTIES OUTPUT_NAME "amdhip32")
endif()

# Disable versioning for Windows
# as currently HIP_LIB_VERSION_STRING and HIP_LIB_VERSION_MAJOR
# are not being populated
if(NOT WIN32)
  if(BUILD_SHARED_LIBS)
    set_target_properties(amdhip64 PROPERTIES
      VERSION ${HIP_LIB_VERSION_STRING}
      SOVERSION ${HIP_LIB_VERSION_MAJOR})
  endif()
endif()

target_sources(amdhip64 PRIVATE
  fixme.cpp
  hip_activity.cpp
  hip_code_object.cpp
  hip_context.cpp
  hip_device_runtime.cpp
  hip_device.cpp
  hip_error.cpp
  hip_event.cpp
  hip_event_ipc.cpp
  hip_fatbin.cpp
  hip_global.cpp
  hip_graph_internal.cpp
  hip_graph.cpp
  hip_hmm.cpp
  hip_intercept.cpp
  hip_memory.cpp
  hip_mempool.cpp
  hip_mempool_impl.cpp
  hip_module.cpp
  hip_peer.cpp
  hip_platform.cpp
  hip_profile.cpp
  hip_stream_ops.cpp
  hip_stream.cpp
  hip_surface.cpp
  hip_texture.cpp
  hip_gl.cpp
  hip_vm.cpp)

if(WIN32)
  target_sources(amdhip64 PRIVATE
    hip_runtime.cpp)
endif()

if(BUILD_SHARED_LIBS)
  if(WIN32)
    target_sources(amdhip64 PRIVATE amdhip.def)
  else()
    target_link_libraries(amdhip64 PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/hip_hcc.map.in")
    set_target_properties(amdhip64 PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/hip_hcc.map.in")
  endif()
endif()

if(WIN32)
  configure_file(hip_hcc_in.rc.in hip_hcc_info.rc @ONLY)
  target_sources(amdhip64 PRIVATE hip_hcc_info.rc)
endif()

target_include_directories(amdhip64
  PRIVATE
    ${HIP_COMMON_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include)

target_compile_definitions(amdhip64 PRIVATE __HIP_PLATFORM_AMD__)
target_link_libraries(amdhip64 PRIVATE ${OPENGL_LIBRARIES})
target_link_libraries(amdhip64 PRIVATE ${CMAKE_DL_LIBS})

# Note in static case we cannot link against rocclr.
# If we would, we'd also have to export rocclr and have hipcc pass it to the linker.
if(BUILD_SHARED_LIBS)
  target_link_libraries(amdhip64 PRIVATE rocclr)
else()
  target_compile_definitions(amdhip64 PRIVATE $<TARGET_PROPERTY:rocclr,COMPILE_DEFINITIONS>)
  target_include_directories(amdhip64 PRIVATE $<TARGET_PROPERTY:rocclr,INCLUDE_DIRECTORIES>)
endif()

if(DISABLE_DIRECT_DISPATCH)
  target_compile_definitions(amdhip64 PRIVATE DISABLE_DIRECT_DISPATCH)
endif()

# Short-Term solution for pre-compiled headers for online compilation
# Enable pre compiled header
if(__HIP_ENABLE_PCH)
  find_package(LLVM REQUIRED CONFIG
    PATHS
      ${ROCM_PATH}/llvm)
  # find_package(LLVM) returns the lib/cmake/llvm location. We require the root.
  if(NOT DEFINED HIP_LLVM_ROOT)
    set(HIP_LLVM_ROOT "${LLVM_DIR}/../../..")
  endif()

  execute_process(COMMAND sh -c "${CMAKE_CURRENT_SOURCE_DIR}/hip_embed_pch.sh ${HIP_COMMON_INCLUDE_DIR} ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include ${HIP_LLVM_ROOT}" COMMAND_ECHO STDERR RESULT_VARIABLE EMBED_PCH_RC WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  if (EMBED_PCH_RC AND NOT EMBED_PCH_RC EQUAL 0)
    message(FATAL_ERROR "Failed to embed PCH")
  endif()

  target_compile_definitions(amdhip64 PRIVATE __HIP_ENABLE_PCH)
  target_sources(amdhip64 PRIVATE ${CMAKE_BINARY_DIR}/hip_pch.o)
endif()

set(HIPRTC_OBJECTS)
# Add hiprtc
add_subdirectory(hiprtc)

if(NOT WIN32)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(amdhip64 PRIVATE ${HIPRTC_OBJECTS})
    target_compile_definitions(amdhip64 PRIVATE __HIP_ENABLE_RTC)
    add_dependencies(amdhip64 hiprtc-builtins)
    INSTALL(TARGETS hiprtc-builtins
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
endif()

#############################
# Profiling API support
#############################
# Generate profiling API macros/structures header
option(USE_PROF_API "Enable roctracer integration" ON)
# Enable profiling API
if(USE_PROF_API)
  set(PROF_API_STR "${PROJECT_BINARY_DIR}/include/hip/amd_detail/hip_prof_str.h")
  set(PROF_API_STR_IN "${CMAKE_SOURCE_DIR}/hipamd/include/hip/amd_detail/hip_prof_str.h")
  set(PROF_API_HDR "${HIP_COMMON_INCLUDE_DIR}/hip/hip_runtime_api.h")
  set(PROF_API_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
  set(PROF_API_GEN "${CMAKE_CURRENT_SOURCE_DIR}/hip_prof_gen.py")
  set(PROF_API_LOG "${PROJECT_BINARY_DIR}/hip_prof_gen.log.txt")

  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import CppHeaderParser"
                  RESULT_VARIABLE CPP_HEADER_PARSER
                  OUTPUT_QUIET)

  if(NOT ${CPP_HEADER_PARSER} EQUAL 0)
    message(FATAL_ERROR "\
The \"CppHeaderParser\" Python3 package is not installed. \
Please install it using the following command: \"pip3 install CppHeaderParser\".\
")
  endif()

  add_custom_command(OUTPUT ${PROF_API_STR}
    COMMAND ${Python3_EXECUTABLE} ${PROF_API_GEN} -v -t --priv ${PROF_API_HDR} ${PROF_API_SRC} ${PROF_API_STR_IN} ${PROF_API_STR}
    DEPENDS ${PROF_API_STR_IN} ${PROF_API_HDR} ${PROF_API_GEN}
    COMMENT "Generating profiling primitives: ${PROF_API_STR}")

  add_custom_target(gen-prof-api-str-header ALL
    DEPENDS ${PROF_API_STR}
    SOURCES ${PROF_API_HDR})

  set_target_properties(amdhip64 PROPERTIES PUBLIC_HEADER ${PROF_API_STR})

  find_path(PROF_API_HEADER_DIR prof_protocol.h
    HINTS
      ${PROF_API_HEADER_PATH}
    PATHS
      ${ROCM_PATH}/roctracer
    PATH_SUFFIXES
      include/ext)

  if(NOT PROF_API_HEADER_DIR)
    message(WARNING "Profiling API header not found. Disabling roctracer integration. Use -DPROF_API_HEADER_PATH=<path to prof_protocol.h header>")
  else()
    target_compile_definitions(amdhip64 PUBLIC USE_PROF_API=1)
    target_include_directories(amdhip64 PUBLIC ${PROF_API_HEADER_DIR})
    message(STATUS "Profiling API: ${PROF_API_HEADER_DIR}")
  endif()

  add_dependencies(amdhip64 gen-prof-api-str-header)
endif()

add_custom_command(TARGET amdhip64 POST_BUILD COMMAND
  ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/.hipInfo ${PROJECT_BINARY_DIR}/lib/.hipInfo)
add_custom_command(TARGET amdhip64 POST_BUILD COMMAND
  ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)

add_library(host INTERFACE)
target_link_libraries(host INTERFACE amdhip64)

add_library(device INTERFACE)
target_link_libraries(device INTERFACE host)

# Current packaging assumes that HIP runtime will always be installed in ${ROCM_PATH}/lib
# This is false to assume, because some distros like CentOS will use the lib64 directory instead of lib
# Relying on CMake to choose the library directory for us will default in that case to lib64
# Hence there will be a mismatch between where HIP is installed and where CMake thinks it is

INSTALL(TARGETS amdhip64 host device
  EXPORT hip-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
INSTALL(EXPORT hip-targets DESTINATION ${CONFIG_PACKAGE_INSTALL_DIR} NAMESPACE hip::)

INSTALL(TARGETS amdhip64 host device
  EXPORT hip-lang-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
INSTALL(EXPORT hip-lang-targets DESTINATION ${CONFIG_LANG_PACKAGE_INSTALL_DIR} NAMESPACE hip-lang::)

if(NOT WIN32)
include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${HIP_COMMON_DIR}/hip-lang-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/hip-lang-config.cmake
  INSTALL_DESTINATION ${CONFIG_LANG_PACKAGE_INSTALL_DIR}
  PATH_VARS LIB_INSTALL_DIR INCLUDE_INSTALL_DIR BIN_INSTALL_DIR)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/hip-lang-config-version.cmake
  VERSION "${HIP_VERSION_MAJOR}.${HIP_VERSION_MINOR}.${HIP_VERSION_GITDATE}"
  COMPATIBILITY SameMajorVersion)
install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hip-lang-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hip-lang-config-version.cmake
    DESTINATION
    ${CONFIG_LANG_PACKAGE_INSTALL_DIR}/
    )
endif()
