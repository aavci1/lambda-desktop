if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  add_executable(lambda-status-notifier-watcher main.cpp)
  target_link_libraries(lambda-status-notifier-watcher PRIVATE lambda)

  if(LAMBDA_ENABLE_ASAN)
    target_compile_options(lambda-status-notifier-watcher PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-status-notifier-watcher PRIVATE -fsanitize=address)
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.kde.StatusNotifierWatcher.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.kde.StatusNotifierWatcher.service"
    @ONLY)

  install(TARGETS lambda-status-notifier-watcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.kde.StatusNotifierWatcher.service"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
endif()
