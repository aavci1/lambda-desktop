find_package(PkgConfig REQUIRED)

pkg_check_modules(LAMBDA_BROWSER_WPE QUIET IMPORTED_TARGET
  wpe-webkit-2.0
  wpebackend-fdo-1.0
  gbm
  libdrm)

if(NOT LAMBDA_BROWSER_WPE_FOUND)
  message(STATUS "Skipping lambda-browser: WPE WebKit/FDO pkg-config packages were not found")
  return()
endif()

lambda_add_app(lambda-browser
  APP_NAME "Lambda Browser"
  BUNDLE_IDENTIFIER "io.lambda.browser"
  COMMENT "Simple WPE WebKit browser for Lambda Desktop"
  SOURCES main.cpp)

target_link_libraries(lambda-browser PRIVATE PkgConfig::LAMBDA_BROWSER_WPE)
