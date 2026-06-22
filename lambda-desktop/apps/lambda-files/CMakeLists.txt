lambda_add_app(lambda-files
  APP_NAME "Lambda Files"
  BUNDLE_IDENTIFIER "io.lambda.files"
  COMMENT "File manager for Lambda Desktop"
  SOURCES
    main.cpp
    FilesStore.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellAppRegistry.cpp)

target_include_directories(lambda-files PRIVATE
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell)
target_link_libraries(lambda-files PRIVATE tomlplusplus::tomlplusplus)
