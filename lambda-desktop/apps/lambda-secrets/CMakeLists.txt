if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  add_executable(lambda-secrets main.cpp)
  target_link_libraries(lambda-secrets PRIVATE lambda)

  if(LAMBDA_ENABLE_ASAN)
    target_compile_options(lambda-secrets PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-secrets PRIVATE -fsanitize=address)
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.freedesktop.secrets.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.secrets.service"
    @ONLY)

  install(TARGETS lambda-secrets RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.secrets.service"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
endif()
