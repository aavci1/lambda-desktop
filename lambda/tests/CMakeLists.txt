target_compile_definitions(lambda PRIVATE LAMBDA_TESTING=1)

set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "" FORCE)
if(POLICY CMP0127)
  cmake_policy(SET CMP0127 NEW)
endif()
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
)
set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
FetchContent_MakeAvailable(doctest)

set(_lambda_framework_test_sources
  AlertTests.cpp
  AnimationTests.cpp
  BindableInferenceTests.cpp
  EnvironmentKeyOtherTU.cpp
  EnvironmentKeyTests.cpp
  EventQueueTests.cpp
  FileDialogTests.cpp
  ForTests.cpp
  LayoutAlgorithmTests.cpp
  main.cpp
  MountRootTests.cpp
  ParagraphCacheTests.cpp
  ParagraphCacheIncrementalTests.cpp
  PathFlattenerTests.cpp
  RuntimeInputTests.cpp
  RuntimeTests.cpp
  SceneGraphTests.cpp
  SceneTraversalTests.cpp
  ShowTests.cpp
  SvgPathTests.cpp
  TextEditUtilsTests.cpp
  TextInputScrollTests.cpp
  ThemeReactivityTests.cpp)

if(LAMBDA_PLATFORM_MACOS)
  list(APPEND _lambda_framework_test_sources MetalCanvasTests.mm)
endif()

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  list(APPEND _lambda_framework_test_sources
    FreeTypeTextSystemTests.cpp
    VulkanRenderTargetTests.cpp
    WaylandScrollAccumulatorTests.cpp)
endif()

add_executable(lambda_framework_tests ${_lambda_framework_test_sources})
target_include_directories(lambda_framework_tests PRIVATE
  ${LAMBDA_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDA_FRAMEWORK_SOURCE_DIR}
  ${LAMBDA_FRAMEWORK_PROTOCOL_DIR}/wayland)
target_link_libraries(lambda_framework_tests PRIVATE
  lambda
  doctest::doctest)

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  target_link_libraries(lambda_framework_tests PRIVATE
    PkgConfig::VULKAN
    PkgConfig::LIBDRM
    PkgConfig::ZLIB)
  if(DEFINED LAMBDA_WAYLAND_PROTOCOL_DIR)
    target_include_directories(lambda_framework_tests PRIVATE ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  endif()
endif()

if(LAMBDA_PLATFORM_MACOS)
  target_link_libraries(lambda_framework_tests PRIVATE
    "-framework Foundation"
    "-framework Metal")
endif()

target_compile_definitions(lambda_framework_tests PRIVATE LAMBDA_TESTING=1)
set_target_properties(lambda_framework_tests PROPERTIES CXX_STANDARD 23)
if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_framework_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_framework_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_framework_tests COMMAND lambda_framework_tests)

add_custom_target(lambda_tests DEPENDS lambda_framework_tests)

add_executable(lambda_reactive_tests
  Reactive/main.cpp
  Reactive/SignalTests.cpp
  Reactive/ComputedTests.cpp
  Reactive/EffectTests.cpp
  Reactive/ScopeTests.cpp
  Reactive/DiamondTests.cpp
  Reactive/BindableTests.cpp)
target_include_directories(lambda_reactive_tests PRIVATE
  ${LAMBDA_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDA_FRAMEWORK_SOURCE_DIR})
target_link_libraries(lambda_reactive_tests PRIVATE lambda doctest::doctest)
set_target_properties(lambda_reactive_tests PROPERTIES CXX_STANDARD 23)
target_compile_definitions(lambda_reactive_tests PRIVATE LAMBDA_TESTING=1)
if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_reactive_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_reactive_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_reactive_tests COMMAND lambda_reactive_tests)
