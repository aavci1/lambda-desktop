cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lambda-desktop VERSION 5.0.0 LANGUAGES CXX C)

  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  include(GNUInstallDirs)

  set(_lambda_desktop_default_lambdaui "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../lambdaui/CMakeLists.txt")
    get_filename_component(_lambda_desktop_default_lambdaui
      "${CMAKE_CURRENT_SOURCE_DIR}/../lambdaui" ABSOLUTE)
  endif()
  set(LAMBDAUI_SOURCE_DIR "${_lambda_desktop_default_lambdaui}" CACHE PATH
      "Path to a local lambdaui source checkout. Required while lambda-desktop uses framework private headers.")
  if(LAMBDAUI_SOURCE_DIR)
    set(LAMBDAUI_FRAMEWORK_DIR "${LAMBDAUI_SOURCE_DIR}")
    set(LAMBDAUI_FRAMEWORK_INCLUDE_DIR "${LAMBDAUI_FRAMEWORK_DIR}/include")
    set(LAMBDAUI_FRAMEWORK_SOURCE_DIR "${LAMBDAUI_FRAMEWORK_DIR}/src")
    set(LAMBDAUI_FRAMEWORK_CMAKE_DIR "${LAMBDAUI_FRAMEWORK_DIR}/cmake")
    set(LAMBDAUI_FRAMEWORK_RESOURCE_DIR "${LAMBDAUI_FRAMEWORK_DIR}/resources")
    set(LAMBDAUI_FRAMEWORK_PROTOCOL_DIR "${LAMBDAUI_FRAMEWORK_DIR}/protocols")
    set(LAMBDAUI_FRAMEWORK_VENDOR_DIR "${LAMBDAUI_FRAMEWORK_DIR}/vendor")
    add_subdirectory("${LAMBDAUI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/lambdaui")
  else()
    message(FATAL_ERROR
      "lambda-desktop currently requires a local lambdaui source checkout. "
      "Configure with -DLAMBDAUI_SOURCE_DIR=/path/to/lambdaui.")
  endif()

  option(LAMBDA_BUILD_TESTS "Build lambda-desktop tests." OFF)
  option(LAMBDA_BUILD_TOOLS "Build developer verification tools." ON)
  option(LAMBDA_BUILD_WINDOW_MANAGER
    "Build the Linux KMS lambda-window-manager executable."
    ON)
endif()

if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "lambda-desktop is only supported on Linux.")
endif()

include(FetchContent)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(lambda_desktop_system STATIC
  src/System/BlueZ.cpp
  src/System/DBus.cpp
  src/System/Logind.cpp
  src/System/MPRIS.cpp
  src/System/NetworkManager.cpp
  src/System/Notifications.cpp
  src/System/PolkitAgent.cpp
  src/System/PortalAccount.cpp
  src/System/PortalAppChooser.cpp
  src/System/PortalFileChooser.cpp
  src/System/PortalInhibit.cpp
  src/System/PortalNotification.cpp
  src/System/PortalScreenCast.cpp
  src/System/PortalSettings.cpp
  src/System/Secrets.cpp
  src/System/StatusNotifierWatcher.cpp
  src/System/UDisks2.cpp
  src/System/UPower.cpp)

target_include_directories(lambda_desktop_system PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LAMBDAUI_FRAMEWORK_INCLUDE_DIR})
target_include_directories(lambda_desktop_system PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LAMBDAUI_FRAMEWORK_SOURCE_DIR})
target_compile_features(lambda_desktop_system PUBLIC cxx_std_23)
target_compile_options(lambda_desktop_system PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(lambda_desktop_system PUBLIC LAMBDA_HAS_DBUS=1)
target_link_libraries(lambda_desktop_system PUBLIC
  lambdaui
  PkgConfig::SYSTEMD)
target_link_libraries(lambda_desktop_system PRIVATE
  tomlplusplus::tomlplusplus)

if(LAMBDAUI_ENABLE_ASAN)
  target_compile_options(lambda_desktop_system PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_desktop_system PRIVATE -fsanitize=address)
endif()

set(LAMBDA_DESKTOP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-editor ${CMAKE_CURRENT_BINARY_DIR}/lambda-editor)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-files ${CMAKE_CURRENT_BINARY_DIR}/lambda-files)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-notifications ${CMAKE_CURRENT_BINARY_DIR}/lambda-notifications)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-polkit-agent ${CMAKE_CURRENT_BINARY_DIR}/lambda-polkit-agent)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-portal ${CMAKE_CURRENT_BINARY_DIR}/lambda-portal)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-preview ${CMAKE_CURRENT_BINARY_DIR}/lambda-preview)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-secrets ${CMAKE_CURRENT_BINARY_DIR}/lambda-secrets)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-settings ${CMAKE_CURRENT_BINARY_DIR}/lambda-settings)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell ${CMAKE_CURRENT_BINARY_DIR}/lambda-shell)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-status-notifier-watcher ${CMAKE_CURRENT_BINARY_DIR}/lambda-status-notifier-watcher)
add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-terminal ${CMAKE_CURRENT_BINARY_DIR}/lambda-terminal)

foreach(_lambda_desktop_system_target IN ITEMS
    lambda-files
    lambda-notifications
    lambda-polkit-agent
    lambda-portal
    lambda-secrets
    lambda-shell
    lambda-status-notifier-watcher)
  if(TARGET ${_lambda_desktop_system_target})
    target_link_libraries(${_lambda_desktop_system_target} PRIVATE lambda_desktop_system)
  endif()
endforeach()

if(LAMBDA_BUILD_WINDOW_MANAGER)
  add_subdirectory(${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager ${CMAKE_CURRENT_BINARY_DIR}/lambda-window-manager)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(LAMBDA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()

  if(LAMBDA_BUILD_TOOLS)
    add_subdirectory(tools)
  endif()
endif()
