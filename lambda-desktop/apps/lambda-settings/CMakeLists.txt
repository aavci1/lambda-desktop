lambda_add_app(lambda-settings
  APP_NAME "Lambda Settings"
  BUNDLE_IDENTIFIER "io.lambda.settings"
  COMMENT "Settings for Lambda Desktop"
  SOURCES
    main.cpp
    SettingsBackend.cpp)

target_link_libraries(lambda-settings PRIVATE tomlplusplus::tomlplusplus)
