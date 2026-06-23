lambdaui_add_app(lambda-shell
  APP_NAME "Lambda Shell"
  BUNDLE_IDENTIFIER "io.lambda.shell"
  COMMENT "Desktop shell for Lambda Desktop"
  SOURCES
    Shell/LambdaShell.cpp
    Shell/ShellAppRegistry.cpp
    Shell/ShellAudioControl.cpp
    Shell/ShellConnection.cpp
    Shell/ShellController.cpp
    Shell/ShellIpc.cpp
    Shell/ShellJson.cpp
    Shell/ShellModel.cpp
    Shell/ShellModels.cpp
    Shell/ShellSystemStatus.cpp
    Shell/UI/LambdaCommandLauncher.cpp
    Shell/UI/LambdaDock.cpp
    Shell/UI/LambdaShellTypes.cpp)

target_include_directories(lambda-shell PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lambda-shell PRIVATE tomlplusplus::tomlplusplus)

if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBPULSE QUIET IMPORTED_TARGET libpulse)
    if(LIBPULSE_FOUND)
      target_compile_definitions(lambda-shell PRIVATE LAMBDA_SHELL_HAVE_LIBPULSE=1)
      target_link_libraries(lambda-shell PRIVATE PkgConfig::LIBPULSE)
    endif()
  endif()
endif()
