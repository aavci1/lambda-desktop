cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.0")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(lambda-desktop VERSION 5.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LambdaApp.cmake)

set(LAMBDA_PLATFORM "AUTO" CACHE STRING
    "Lambda backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS.")
set_property(CACHE LAMBDA_PLATFORM PROPERTY STRINGS AUTO MACOS LINUX_WAYLAND LINUX_KMS)

if(LAMBDA_PLATFORM STREQUAL "AUTO")
  if(APPLE)
    set(LAMBDA_PLATFORM_MACOS ON)
    set(LAMBDA_PLATFORM "MACOS" CACHE STRING
      "Lambda backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS." FORCE)
  elseif(UNIX)
    set(LAMBDA_PLATFORM_LINUX_WAYLAND ON)
    set(LAMBDA_PLATFORM "LINUX_WAYLAND" CACHE STRING
      "Lambda backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS." FORCE)
  else()
    message(FATAL_ERROR "Lambda: no default backend for this host.")
  endif()
elseif(LAMBDA_PLATFORM STREQUAL "MACOS")
  if(NOT APPLE)
    message(FATAL_ERROR "LAMBDA_PLATFORM=MACOS requires an Apple host.")
  endif()
  set(LAMBDA_PLATFORM_MACOS ON)
elseif(LAMBDA_PLATFORM STREQUAL "LINUX_WAYLAND")
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "LAMBDA_PLATFORM=LINUX_WAYLAND requires a Linux/Unix host.")
  endif()
  set(LAMBDA_PLATFORM_LINUX_WAYLAND ON)
elseif(LAMBDA_PLATFORM STREQUAL "LINUX_KMS")
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "LAMBDA_PLATFORM=LINUX_KMS requires a Linux host.")
  endif()
  set(LAMBDA_PLATFORM_LINUX_KMS ON)
else()
  message(FATAL_ERROR "Lambda: LAMBDA_PLATFORM=${LAMBDA_PLATFORM} is not implemented yet.")
endif()

if(LAMBDA_PLATFORM_MACOS)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

FetchContent_Declare(libtess2
  GIT_REPOSITORY https://github.com/memononen/libtess2.git
  # Pinned for reproducible builds (master as of 2025-10-16).
  GIT_TAG 8dbd6483e920311a58c9af10a10beb278efebc36
)
FetchContent_MakeAvailable(libtess2)
if(NOT TARGET libtess2)
  add_library(libtess2 STATIC
    ${libtess2_SOURCE_DIR}/Source/bucketalloc.c
    ${libtess2_SOURCE_DIR}/Source/dict.c
    ${libtess2_SOURCE_DIR}/Source/geom.c
    ${libtess2_SOURCE_DIR}/Source/mesh.c
    ${libtess2_SOURCE_DIR}/Source/priorityq.c
    ${libtess2_SOURCE_DIR}/Source/sweep.c
    ${libtess2_SOURCE_DIR}/Source/tess.c
  )
  set_target_properties(libtess2 PROPERTIES LINKER_LANGUAGE C)
endif()
set_target_properties(libtess2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
set_target_properties(libtess2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(libtess2 PUBLIC
  $<BUILD_INTERFACE:${libtess2_SOURCE_DIR}/Include>
)

FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

option(LAMBDA_ENABLE_DEFAULT_EVENT_LOGGING
  "Print built-in Application event handlers to stdout (disable for quiet or embedded builds)."
  OFF)
option(LAMBDA_ENABLE_ASAN
  "Build Lambda targets with AddressSanitizer instrumentation."
  OFF)
option(LAMBDA_PROFILE_REACTIVE
  "Compile deterministic reactive profiling counters and 5-second display-link reports."
  OFF)
option(LAMBDA_DISABLE_VARIANT_REFS
  "Deep-copy paragraph cache layouts (revert to clone on fast path); disables variant shared_ptr retention."
  OFF)
option(LAMBDA_ENABLE_PARAGRAPH_CACHE_PARALLEL_ASSERT
  "Debug: compare incremental vs full paragraph-cache assembly (non-NDEBUG only)."
  OFF)

option(LAMBDA_BUILD_APPS "Build Lambda desktop apps." ON)
option(LAMBDA_BUILD_DEMOS "Build standalone demos under demos/." OFF)
option(LAMBDA_BUILD_TESTS "Build unit and integration tests under tests/." OFF)
option(LAMBDA_BUILD_BENCHMARKS "Build micro-benchmarks in bench/." OFF)

set(LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT OFF)
if(UNIX AND NOT APPLE)
  set(LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT ON)
endif()
option(LAMBDA_BUILD_WINDOW_MANAGER
  "Build the Linux KMS lambda-window-manager executable."
  ${LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT})

add_subdirectory(src)

if(LAMBDA_BUILD_APPS)
  add_subdirectory(apps)
endif()

if(LAMBDA_BUILD_DEMOS)
  add_subdirectory(demos)
endif()

if(LAMBDA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LAMBDA_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
