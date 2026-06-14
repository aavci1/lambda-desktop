if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "lambda-window-manager is only supported on Linux.")
endif()

include(${PROJECT_SOURCE_DIR}/cmake/LambdaWaylandProtocols.cmake)
find_package(PkgConfig REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
if(NOT WAYLAND_PROTOCOLS_DIR)
  message(FATAL_ERROR "Lambda: wayland-protocols did not provide pkgdatadir.")
endif()

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

lambda_setup_wayland_protocol_paths("${WAYLAND_PROTOCOLS_DIR}")
add_library(lambda_wayland_server_protocols OBJECT)
target_include_directories(lambda_wayland_server_protocols PUBLIC ${LAMBDA_WAYLAND_PROTOCOL_DIR})
lambda_wayland_server_protocols(lambda_wayland_server_protocols
  "${LAMBDA_XDG_SHELL_XML}|xdg-shell"
  "${LAMBDA_XDG_DECORATION_XML}|xdg-decoration-unstable-v1"
  "${LAMBDA_XDG_OUTPUT_XML}|xdg-output-unstable-v1"
  "${LAMBDA_VIEWPORTER_XML}|viewporter"
  "${LAMBDA_FRACTIONAL_SCALE_XML}|fractional-scale-v1"
  "${LAMBDA_WLR_LAYER_SHELL_XML}|wlr-layer-shell-unstable-v1"
  "${LAMBDA_PRESENTATION_TIME_XML}|presentation-time"
  "${LAMBDA_RELATIVE_POINTER_XML}|relative-pointer-unstable-v1"
  "${LAMBDA_POINTER_CONSTRAINTS_XML}|pointer-constraints-unstable-v1"
  "${LAMBDA_PRIMARY_SELECTION_XML}|primary-selection-unstable-v1"
  "${LAMBDA_LINUX_DMABUF_XML}|linux-dmabuf-unstable-v1"
  "${LAMBDA_CURSOR_SHAPE_XML}|cursor-shape-v1"
  "${LAMBDA_IDLE_INHIBIT_XML}|idle-inhibit-unstable-v1"
  "${LAMBDA_XDG_ACTIVATION_XML}|xdg-activation-v1"
  "${LAMBDA_EXT_BACKGROUND_EFFECT_XML}|ext-background-effect-v1"
  "${LAMBDA_XX_CUTOUTS_XML}|xx-cutouts-v1")

file(GLOB_RECURSE LAMBDA_WINDOW_MANAGER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Compositor/*.cpp")

lambda_add_app(lambda-window-manager
  APP_NAME "Lambda Window Manager"
  BUNDLE_IDENTIFIER "io.lambda.window-manager"
  COMMENT "Window manager for Lambda Desktop"
  SOURCES
    ${LAMBDA_WINDOW_MANAGER_SOURCES}
    ${PROJECT_SOURCE_DIR}/protocols/wayland/tablet-v2-tool-interface-stub.c
    ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAppRegistry.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellIpc.cpp
    ${PROJECT_SOURCE_DIR}/src/Platform/Linux/KmsApplication.cpp
    ${PROJECT_SOURCE_DIR}/src/Platform/Linux/KmsInput.cpp
    ${PROJECT_SOURCE_DIR}/src/Platform/Linux/KmsOutput.cpp
    ${PROJECT_SOURCE_DIR}/src/Platform/Linux/KmsWindow.cpp)

target_include_directories(lambda-window-manager PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/protocols/wayland
  ${LAMBDA_WAYLAND_PROTOCOL_DIR})
target_compile_options(lambda-window-manager PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lambda-window-manager PRIVATE
  lambda
  lambda_wayland_server_protocols
  tomlplusplus::tomlplusplus
  PkgConfig::WAYLAND_SERVER
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::LIBDRM
  PkgConfig::GBM
  PkgConfig::LIBINPUT
  PkgConfig::LIBSEAT
  PkgConfig::LIBUDEV
  PkgConfig::XKBCOMMON
  PkgConfig::VULKAN
  PkgConfig::FREETYPE
  PkgConfig::FONTCONFIG
  PkgConfig::HARFBUZZ
  PkgConfig::ZLIB
  ${CMAKE_DL_LIBS})
target_link_options(lambda-window-manager PRIVATE -rdynamic)
