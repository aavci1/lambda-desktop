if(LAMBDA_PLATFORM_LINUX_WAYLAND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

  add_executable(lambda-presentation-feedback-check
    lambda-presentation-feedback-check.cpp)
  target_include_directories(lambda-presentation-feedback-check PRIVATE
    ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  target_link_libraries(lambda-presentation-feedback-check PRIVATE
    lambda_wayland_client_protocols
    PkgConfig::WAYLAND_CLIENT)
  target_compile_options(lambda-presentation-feedback-check PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(lambda-presentation-feedback-check PROPERTIES CXX_STANDARD 23)

  add_executable(lambda-frame-artifact-check
    lambda-frame-artifact-check.cpp)
  target_link_libraries(lambda-frame-artifact-check PRIVATE lambda::lambda)
  target_compile_options(lambda-frame-artifact-check PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(lambda-frame-artifact-check PROPERTIES CXX_STANDARD 23)
endif()
