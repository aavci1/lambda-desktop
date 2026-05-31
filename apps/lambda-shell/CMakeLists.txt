lambda_add_app(lambda-shell
  APP_NAME "Lambda Shell"
  BUNDLE_IDENTIFIER "io.lambda.shell"
  COMMENT "Desktop shell for Lambda Desktop"
  SOURCES
    Shell/LambdaShell.cpp
    Shell/ShellAppRegistry.cpp
    Shell/ShellConnection.cpp
    Shell/ShellController.cpp
    Shell/ShellIpc.cpp
    Shell/ShellJson.cpp
    Shell/ShellModel.cpp
    Shell/ShellModels.cpp
    Shell/UI/LambdaCommandLauncher.cpp
    Shell/UI/LambdaDock.cpp
    Shell/UI/LambdaShellTypes.cpp)

target_include_directories(lambda-shell PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lambda-shell PRIVATE tomlplusplus::tomlplusplus)
