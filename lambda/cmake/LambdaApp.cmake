# Cross-platform helper for terminal-style Lambda apps and demos.

function(lambdaui_copy_runtime_resources target_name)
  if(DEFINED LAMBDAUI_FRAMEWORK_RESOURCE_DIR)
    set(_lambda_runtime_resource_dir "${LAMBDAUI_FRAMEWORK_RESOURCE_DIR}")
  else()
    get_filename_component(_lambda_framework_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(_lambda_runtime_resource_dir "${_lambda_framework_dir}/resources")
  endif()

  add_custom_command(
    TARGET ${target_name}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_lambda_runtime_resource_dir}/fonts/MaterialSymbolsRounded.ttf"
            "$<TARGET_FILE_DIR:${target_name}>/fonts/MaterialSymbolsRounded.ttf"
    VERBATIM
    COMMENT "Copy bundled fonts for ${target_name}")
endfunction()

function(lambdaui_add_app target)
  cmake_parse_arguments(LA
    ""
    "APP_NAME;BUNDLE_IDENTIFIER;CATEGORY;ICON;COMMENT"
    "SOURCES"
    ${ARGN})

  if(NOT LA_APP_NAME)
    set(LA_APP_NAME "${target}")
  endif()
  if(NOT LA_BUNDLE_IDENTIFIER)
    string(REPLACE "-" "." _lambda_bundle_suffix "${target}")
    set(LA_BUNDLE_IDENTIFIER "io.lambda.${_lambda_bundle_suffix}")
  endif()
  if(NOT LA_CATEGORY)
    set(LA_CATEGORY "Utility")
  endif()
  if(NOT LA_COMMENT)
    set(LA_COMMENT "${LA_APP_NAME}")
  endif()

  set(_lambda_app_sources ${LA_SOURCES} ${LA_UNPARSED_ARGUMENTS})
  if(NOT _lambda_app_sources)
    message(FATAL_ERROR "lambdaui_add_app(${target}): no sources provided")
  endif()

  add_executable(${target} ${_lambda_app_sources})
  lambdaui_copy_runtime_resources(${target})

  if(NOT APPLE)
    string(REPLACE "." "-" _lambda_desktop_id "${LA_BUNDLE_IDENTIFIER}")
    set(_lambda_desktop_file "${CMAKE_CURRENT_BINARY_DIR}/${_lambda_desktop_id}.desktop")
    file(GENERATE OUTPUT "${_lambda_desktop_file}" CONTENT
"[Desktop Entry]
Type=Application
Name=${LA_APP_NAME}
Comment=${LA_COMMENT}
Exec=${target}
Icon=${target}
Categories=${LA_CATEGORY};
Terminal=false
")
    install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${_lambda_desktop_file}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    if(LA_ICON)
      install(FILES "${LA_ICON}"
              DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
              RENAME "${target}.svg")
    endif()
  endif()

  target_link_libraries(${target} PRIVATE lambdaui)
  if(LAMBDAUI_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
endfunction()
