if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  add_executable(lambda-portal main.cpp)
  target_link_libraries(lambda-portal PRIVATE lambdaui)

  if(LAMBDAUI_ENABLE_ASAN)
    target_compile_options(lambda-portal PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-portal PRIVATE -fsanitize=address)
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/org.freedesktop.impl.portal.desktop.lambda.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.lambda.service"
    @ONLY)

  install(TARGETS lambda-portal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.lambda.service"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/lambda.portal"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
endif()
