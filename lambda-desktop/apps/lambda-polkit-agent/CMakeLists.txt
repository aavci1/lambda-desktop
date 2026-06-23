if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  add_executable(lambda-polkit-agent main.cpp)
  target_link_libraries(lambda-polkit-agent PRIVATE lambdaui)

  if(LAMBDAUI_ENABLE_ASAN)
    target_compile_options(lambda-polkit-agent PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(lambda-polkit-agent PRIVATE -fsanitize=address)
  endif()

  install(TARGETS lambda-polkit-agent RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
