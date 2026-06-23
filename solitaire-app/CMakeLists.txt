cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.0")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
  endif()

  project(lambda-solitaire VERSION 1.0 LANGUAGES CXX C)

  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  set(_lambda_solitaire_default_lambdaui "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../lambdaui/CMakeLists.txt")
    get_filename_component(_lambda_solitaire_default_lambdaui
      "${CMAKE_CURRENT_SOURCE_DIR}/../lambdaui" ABSOLUTE)
  endif()
  set(LAMBDAUI_SOURCE_DIR "${_lambda_solitaire_default_lambdaui}" CACHE PATH
      "Path to a local lambdaui source checkout.")
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
      "lambda-solitaire requires lambdaui. Configure with "
      "-DLAMBDAUI_SOURCE_DIR=/path/to/lambdaui, or place lambdaui next to this repo.")
  endif()
endif()

set(SOLITAIRE_APP_NAME "Solitaire")
set(SOLITAIRE_BUNDLE_IDENTIFIER "io.lambda.solitaire")
set(SOLITAIRE_CATEGORY "public.app-category.card-games")
set(SOLITAIRE_VERSION "1.0")
set(SOLITAIRE_BUILD "1")
set(SOLITAIRE_MIN_MACOS "12.0")
set(SOLITAIRE_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/icon.svg")
if(NOT DEFINED LAMBDAUI_FRAMEWORK_RESOURCE_DIR)
  message(FATAL_ERROR "lambda-solitaire requires lambdaui resources; add lambdaui before this directory.")
endif()
set(SOLITAIRE_FONT "${LAMBDAUI_FRAMEWORK_RESOURCE_DIR}/fonts/MaterialSymbolsRounded.ttf")
set(SOLITAIRE_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/sandbox.entitlements")

string(TIMESTAMP SOLITAIRE_COPYRIGHT_YEAR "%Y")
set(SOLITAIRE_COPYRIGHT "Copyright (c) ${SOLITAIRE_COPYRIGHT_YEAR}")

if(APPLE)
  find_program(SOLITAIRE_QLMANAGE qlmanage)
  find_program(SOLITAIRE_SIPS sips)
  find_program(SOLITAIRE_ICONUTIL iconutil)
  if(NOT SOLITAIRE_QLMANAGE OR NOT SOLITAIRE_SIPS OR NOT SOLITAIRE_ICONUTIL)
    message(FATAL_ERROR
      "solitaire-app requires qlmanage, sips, and iconutil to build its macOS app icon.")
  endif()

  set(SOLITAIRE_ICON_ICNS "${CMAKE_CURRENT_BINARY_DIR}/solitaire-app.icns")
  set(SOLITAIRE_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/solitaire-app.iconset")
  set(SOLITAIRE_ICON_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/solitaire-app-icon-work")
  add_custom_command(OUTPUT "${SOLITAIRE_ICON_ICNS}"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=${SOLITAIRE_ICON_SVG}"
      "-DOUTPUT=${SOLITAIRE_ICON_ICNS}"
      "-DICONSET=${SOLITAIRE_ICONSET}"
      "-DWORK_DIR=${SOLITAIRE_ICON_WORK_DIR}"
      "-DQLMANAGE=${SOLITAIRE_QLMANAGE}"
      "-DSIPS=${SOLITAIRE_SIPS}"
      "-DICONUTIL=${SOLITAIRE_ICONUTIL}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/make-icns-from-svg.cmake"
    DEPENDS "${SOLITAIRE_ICON_SVG}" "${CMAKE_CURRENT_SOURCE_DIR}/make-icns-from-svg.cmake"
    VERBATIM
    COMMENT "Build solitaire-app app icon from SVG")

  get_filename_component(SOLITAIRE_BUNDLE_ICON_FILE "${SOLITAIRE_ICON_ICNS}" NAME)
  set(SOLITAIRE_PLIST "${CMAKE_CURRENT_BINARY_DIR}/solitaire-app-Info.plist")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    "${SOLITAIRE_PLIST}"
    @ONLY)

  add_executable(lambda-solitaire MACOSX_BUNDLE
    main.cpp
    "${SOLITAIRE_ICON_ICNS}"
    "${SOLITAIRE_FONT}")

  set_source_files_properties("${SOLITAIRE_ICON_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
    GENERATED TRUE)
  set_source_files_properties("${SOLITAIRE_FONT}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/fonts")

  set_target_properties(lambda-solitaire PROPERTIES
    OUTPUT_NAME "${SOLITAIRE_APP_NAME}"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${SOLITAIRE_PLIST}"
    MACOSX_BUNDLE_BUNDLE_NAME "${SOLITAIRE_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${SOLITAIRE_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${SOLITAIRE_BUILD}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${SOLITAIRE_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "${SOLITAIRE_COPYRIGHT}"
    MACOSX_BUNDLE_ICON_FILE "${SOLITAIRE_BUNDLE_ICON_FILE}"
    XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${SOLITAIRE_MIN_MACOS}"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${SOLITAIRE_ENTITLEMENTS}"
    OSX_ARCHITECTURES "arm64;x86_64")

  foreach(_solitaire_dep IN ITEMS lambdaui libtess2)
    if(TARGET ${_solitaire_dep})
      set_target_properties(${_solitaire_dep} PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
    endif()
  endforeach()

  add_custom_target(lambda-solitaire-package
    COMMAND /bin/bash -c
            "exec /bin/bash \"${CMAKE_CURRENT_SOURCE_DIR}/sign-and-package.sh\" \"$<TARGET_BUNDLE_DIR:lambda-solitaire>\" \"$LAMBDA_SIGN_APP_ID\" \"$LAMBDA_SIGN_INSTALLER_ID\" \"${CMAKE_CURRENT_BINARY_DIR}/${SOLITAIRE_APP_NAME}.pkg\" \"${SOLITAIRE_ENTITLEMENTS}\""
    DEPENDS lambda-solitaire
    VERBATIM
    COMMENT "Sign and package ${SOLITAIRE_APP_NAME}")

  target_link_libraries(lambda-solitaire PRIVATE lambdaui)
  if(LAMBDAUI_ENABLE_ASAN)
    target_compile_options(lambda-solitaire PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-solitaire PRIVATE -fsanitize=address)
  endif()
else()
  lambdaui_add_app(lambda-solitaire
    APP_NAME "${SOLITAIRE_APP_NAME}"
    BUNDLE_IDENTIFIER "${SOLITAIRE_BUNDLE_IDENTIFIER}"
    CATEGORY "Game"
    ICON "${SOLITAIRE_ICON_SVG}"
    COMMENT "Solitaire for Lambda"
    SOURCES main.cpp)
endif()
