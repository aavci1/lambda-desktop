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

set(_lambda_test_sources
  AlertTests.cpp
  AnimationTests.cpp
  BindableInferenceTests.cpp
  BlueZTests.cpp
  DBusTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-editor/EditorCommands.cpp
  EditorCommandsTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-editor/EditorDocument.cpp
  EditorDocumentTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-editor/EditorFileWatcher.cpp
  EditorFileWatcherTests.cpp
  EnvironmentKeyOtherTU.cpp
  EnvironmentKeyTests.cpp
  EventQueueTests.cpp
  FileDialogTests.cpp
  FilesFlowGridTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-files/FilesStore.cpp
  FilesStoreTests.cpp
  ForTests.cpp
  LogindTests.cpp
  MPRISTests.cpp
  MountRootTests.cpp
  NetworkManagerTests.cpp
  NotificationsTests.cpp
  main.cpp
  PathFlattenerTests.cpp
  PortalAppChooserTests.cpp
  PortalInhibitTests.cpp
  PortalNotificationTests.cpp
  PolkitAgentTests.cpp
  PortalSettingsTests.cpp
  SceneGraphTests.cpp
  ParagraphCacheTests.cpp
  ParagraphCacheIncrementalTests.cpp
  RuntimeInputTests.cpp
  RuntimeTests.cpp
  ShowTests.cpp
  StatusNotifierWatcherTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-settings/SettingsBackend.cpp
  SettingsBackendTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAppRegistry.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAudioControl.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellConnection.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellIpc.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellJson.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellModels.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/ShellSystemStatus.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/UI/LambdaDock.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell/Shell/UI/LambdaShellTypes.cpp
  ShellAppRegistryTests.cpp
  ShellAudioControlTests.cpp
  ShellConnectionTests.cpp
  ShellDockTests.cpp
  ShellIpcTests.cpp
  ShellModelTests.cpp
  ShellModelsTests.cpp
  ShellSystemStatusTests.cpp
  SecretsTests.cpp
  SvgPathTests.cpp
  ${PROJECT_SOURCE_DIR}/apps/lambda-terminal/TerminalCore.cpp
  TerminalCoreTests.cpp
  TerminalPtySmokeTests.cpp
  TextInputScrollTests.cpp
  ThemeReactivityTests.cpp
  UDisks2Tests.cpp
  UPowerTests.cpp
  WaylandScrollAccumulatorTests.cpp
)

if(LAMBDA_PLATFORM_MACOS)
  list(APPEND _lambda_test_sources MetalCanvasTests.mm)
endif()

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  list(APPEND _lambda_test_sources
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Chrome/WindowChromeRenderer.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Config/CompositorConfig.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/SceneDamage.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Screenshot.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Surface/CommittedSurfacePainter.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Window/WindowGeometry.cpp
    CompositorConfigTests.cpp
    CompositorCursorShapeTests.cpp
    CompositorDataDeviceTests.cpp
    CompositorDecorationStateTests.cpp
    CompositorFrameSchedulerTests.cpp
    FreeTypeTextSystemTests.cpp
    CompositorFractionalScaleTests.cpp
    CompositorGlobalResourceHygieneTests.cpp
    CompositorWindowGeometryTests.cpp
    CompositorWindowFrameGeometryTests.cpp
    CompositorConfigHotReloadTests.cpp
    CompositorIdleInhibitTests.cpp
    CompositorLayerShellZoneTests.cpp
    CompositorOutputSelectorTests.cpp
    CompositorOutputStateTests.cpp
    CompositorPresentationFeedbackTests.cpp
    CompositorPointerExtensionTests.cpp
    CompositorScreenshotTests.cpp
    CompositorSceneDamageTests.cpp
    CompositorSeatSerialTests.cpp
    CompositorSnapshotBuilderTests.cpp
    CompositorSubsurfaceHitTests.cpp
    CompositorSurfaceUploadDamageTests.cpp
    CompositorViewporterTests.cpp
    CompositorVtSwitchShortcutTests.cpp
    CompositorXdgOutputTests.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/LayerShellZones.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/LayerShellState.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/BufferRelease.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/DmabufFeedback.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/DmabufValidation.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/SeatSerials.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/SubsurfaceState.cpp
    ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/PresenterEnv.cpp
    CompositorBufferReleaseTests.cpp
    CompositorDmabufFeedbackTests.cpp
    CompositorDmabufValidationTests.cpp
    VulkanRenderTargetTests.cpp
  )
  if(LAMBDA_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
    list(APPEND _lambda_test_sources CompositorWindowStateTests.cpp)
  endif()
  if(LAMBDA_PLATFORM_LINUX_KMS)
    list(APPEND _lambda_test_sources
      ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager/Compositor/SceneGraph/CompositorSceneGraph.cpp)
  endif()
endif()

add_executable(lambda_tests ${_lambda_test_sources})
target_include_directories(lambda_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/apps/lambda-files
  ${PROJECT_SOURCE_DIR}/apps/lambda-editor
  ${PROJECT_SOURCE_DIR}/apps/lambda-settings
  ${PROJECT_SOURCE_DIR}/apps/lambda-terminal
  ${PROJECT_SOURCE_DIR}/apps/lambda-shell
  ${PROJECT_SOURCE_DIR}/apps/lambda-window-manager
  ${PROJECT_SOURCE_DIR}/protocols/wayland)
target_link_libraries(lambda_tests PRIVATE
  lambda
  tomlplusplus::tomlplusplus
  doctest::doctest)

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  target_link_libraries(lambda_tests PRIVATE
    PkgConfig::VULKAN
    PkgConfig::LIBDRM
    PkgConfig::ZLIB
    util)
  if(LAMBDA_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
    add_dependencies(lambda_tests lambda_wayland_server_protocols)
    target_link_libraries(lambda_tests PRIVATE PkgConfig::WAYLAND_SERVER)
  endif()
  if(DEFINED LAMBDA_WAYLAND_PROTOCOL_DIR)
    target_include_directories(lambda_tests PRIVATE ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  endif()
  pkg_check_modules(LIBPULSE QUIET IMPORTED_TARGET libpulse)
  if(LIBPULSE_FOUND)
    target_compile_definitions(lambda_tests PRIVATE LAMBDA_SHELL_HAVE_LIBPULSE=1)
    target_link_libraries(lambda_tests PRIVATE PkgConfig::LIBPULSE)
  endif()
endif()

if(LAMBDA_PLATFORM_MACOS)
  target_link_libraries(lambda_tests PRIVATE
    "-framework Foundation"
    "-framework Metal")
endif()

target_compile_definitions(lambda_tests PRIVATE LAMBDA_TESTING=1)
if(LAMBDA_PLATFORM_LINUX_KMS)
  target_compile_definitions(lambda_tests PRIVATE LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH=1)
endif()
set_target_properties(lambda_tests PROPERTIES CXX_STANDARD 23)
if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_tests COMMAND lambda_tests)

add_executable(lambda_reactive_tests
  Reactive/main.cpp
  Reactive/SignalTests.cpp
  Reactive/ComputedTests.cpp
  Reactive/EffectTests.cpp
  Reactive/ScopeTests.cpp
  Reactive/DiamondTests.cpp
  Reactive/BindableTests.cpp)
target_include_directories(lambda_reactive_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lambda_reactive_tests PRIVATE lambda doctest::doctest)
set_target_properties(lambda_reactive_tests PROPERTIES CXX_STANDARD 23)
target_compile_definitions(lambda_reactive_tests PRIVATE LAMBDA_TESTING=1)
if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_reactive_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_reactive_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_reactive_tests COMMAND lambda_reactive_tests)
