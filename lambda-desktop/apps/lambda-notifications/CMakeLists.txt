if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  add_executable(lambda-notifications main.cpp)
  target_link_libraries(lambda-notifications PRIVATE lambda)

  if(LAMBDA_ENABLE_ASAN)
    target_compile_options(lambda-notifications PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-notifications PRIVATE -fsanitize=address)
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.freedesktop.Notifications.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.Notifications.service"
    @ONLY)

  install(TARGETS lambda-notifications RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.Notifications.service"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
endif()
