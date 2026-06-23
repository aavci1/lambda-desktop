target_compile_definitions(lambdaui PRIVATE LAMBDAUI_TESTING=1)

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

set(_lambda_test_sources
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
  Reactive/SignalTests.cpp
  Reactive/ComputedTests.cpp
  Reactive/EffectTests.cpp
  Reactive/ScopeTests.cpp
  Reactive/DiamondTests.cpp
  Reactive/BindableTests.cpp
  RuntimeInputTests.cpp
  RuntimeTests.cpp
  SceneGraphTests.cpp
  SceneTraversalTests.cpp
  ShowTests.cpp
  SvgPathTests.cpp
  TextEditUtilsTests.cpp
  TextInputScrollTests.cpp
  ThemeReactivityTests.cpp)

if(LAMBDAUI_PLATFORM_MACOS)
  list(APPEND _lambda_test_sources MetalCanvasTests.mm)
endif()

if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  list(APPEND _lambda_test_sources
    FreeTypeTextSystemTests.cpp
    VulkanRenderTargetTests.cpp
    WaylandScrollAccumulatorTests.cpp)
endif()

add_executable(lambda-tests ${_lambda_test_sources})
target_include_directories(lambda-tests PRIVATE
  ${LAMBDAUI_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDAUI_FRAMEWORK_SOURCE_DIR}
  ${LAMBDAUI_FRAMEWORK_PROTOCOL_DIR}/wayland)
target_link_libraries(lambda-tests PRIVATE
  lambdaui
  doctest::doctest)

if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  target_link_libraries(lambda-tests PRIVATE
    PkgConfig::VULKAN
    PkgConfig::LIBDRM
    PkgConfig::ZLIB)
  if(DEFINED LAMBDA_WAYLAND_PROTOCOL_DIR)
    target_include_directories(lambda-tests PRIVATE ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  endif()
endif()

if(LAMBDAUI_PLATFORM_MACOS)
  target_link_libraries(lambda-tests PRIVATE
    "-framework Foundation"
    "-framework Metal")
endif()

target_compile_definitions(lambda-tests PRIVATE LAMBDAUI_TESTING=1)
set_target_properties(lambda-tests PROPERTIES CXX_STANDARD 23)
if(LAMBDAUI_ENABLE_ASAN)
  target_compile_options(lambda-tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda-tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda-tests COMMAND lambda-tests)

add_custom_target(lambda_tests DEPENDS lambda-tests)
