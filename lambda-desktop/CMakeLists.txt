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
  ${LAMBDA_FRAMEWORK_INCLUDE_DIR})
target_include_directories(lambda_desktop_system PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LAMBDA_FRAMEWORK_SOURCE_DIR})
target_compile_features(lambda_desktop_system PUBLIC cxx_std_23)
target_compile_options(lambda_desktop_system PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(lambda_desktop_system PUBLIC LAMBDA_HAS_DBUS=1)
target_link_libraries(lambda_desktop_system PUBLIC
  lambda
  PkgConfig::SYSTEMD)
target_link_libraries(lambda_desktop_system PRIVATE
  tomlplusplus::tomlplusplus)

if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_desktop_system PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_desktop_system PRIVATE -fsanitize=address)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-editor ${CMAKE_CURRENT_BINARY_DIR}/lambda-editor)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-files ${CMAKE_CURRENT_BINARY_DIR}/lambda-files)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-notifications ${CMAKE_CURRENT_BINARY_DIR}/lambda-notifications)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-polkit-agent ${CMAKE_CURRENT_BINARY_DIR}/lambda-polkit-agent)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-portal ${CMAKE_CURRENT_BINARY_DIR}/lambda-portal)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-preview ${CMAKE_CURRENT_BINARY_DIR}/lambda-preview)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-secrets ${CMAKE_CURRENT_BINARY_DIR}/lambda-secrets)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-settings ${CMAKE_CURRENT_BINARY_DIR}/lambda-settings)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell ${CMAKE_CURRENT_BINARY_DIR}/lambda-shell)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-status-notifier-watcher ${CMAKE_CURRENT_BINARY_DIR}/lambda-status-notifier-watcher)
add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-terminal ${CMAKE_CURRENT_BINARY_DIR}/lambda-terminal)

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
  add_subdirectory(${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager ${CMAKE_CURRENT_BINARY_DIR}/lambda-window-manager)
endif()
