set(_lambda_desktop_test_sources
  BlueZTests.cpp
  DBusTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-editor/EditorCommands.cpp
  EditorCommandsTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-editor/EditorDocument.cpp
  EditorDocumentTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-editor/EditorFileWatcher.cpp
  EditorFileWatcherTests.cpp
  FilesFlowGridTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-files/FilesStore.cpp
  FilesStoreTests.cpp
  LogindTests.cpp
  main.cpp
  MPRISTests.cpp
  NetworkManagerTests.cpp
  NotificationsTests.cpp
  PortalAccountTests.cpp
  PortalAppChooserTests.cpp
  PortalFileChooserTests.cpp
  PortalInhibitTests.cpp
  PortalNotificationTests.cpp
  PortalScreenCastTests.cpp
  PolkitAgentTests.cpp
  PortalSettingsTests.cpp
  SecretsTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-settings/SettingsBackend.cpp
  SettingsBackendTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAppRegistry.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAudioControl.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellConnection.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellIpc.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellJson.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellModel.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellModels.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellSystemStatus.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/UI/LambdaDock.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/UI/LambdaShellTypes.cpp
  ShellAppRegistryTests.cpp
  ShellAudioControlTests.cpp
  ShellConnectionTests.cpp
  ShellDockTests.cpp
  ShellIpcTests.cpp
  ShellModelTests.cpp
  ShellModelsTests.cpp
  ShellSystemStatusTests.cpp
  StatusNotifierWatcherTests.cpp
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-terminal/TerminalCore.cpp
  TerminalCoreTests.cpp
  TerminalPtySmokeTests.cpp
  UDisks2Tests.cpp
  UPowerTests.cpp)

if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  list(APPEND _lambda_desktop_test_sources
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Chrome/WindowChromeRenderer.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Config/CompositorConfig.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/SceneDamage.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Screenshot.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Surface/CommittedSurfacePainter.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Window/WindowGeometry.cpp
    CompositorBufferReleaseTests.cpp
    CompositorConfigHotReloadTests.cpp
    CompositorConfigTests.cpp
    CompositorCursorShapeTests.cpp
    CompositorDataDeviceTests.cpp
    CompositorDecorationStateTests.cpp
    CompositorDmabufFeedbackTests.cpp
    CompositorDmabufValidationTests.cpp
    CompositorFrameSchedulerTests.cpp
    CompositorFractionalScaleTests.cpp
    CompositorGlobalResourceHygieneTests.cpp
    CompositorIdleInhibitTests.cpp
    CompositorLayerShellZoneTests.cpp
    CompositorOutputSelectorTests.cpp
    CompositorOutputStateTests.cpp
    CompositorPointerExtensionTests.cpp
    CompositorPresentationFeedbackTests.cpp
    CompositorVulkanRenderTargetTests.cpp
    CompositorScreenshotTests.cpp
    CompositorSceneDamageTests.cpp
    CompositorSeatSerialTests.cpp
    CompositorSnapshotBuilderTests.cpp
    CompositorSubsurfaceHitTests.cpp
    CompositorSurfaceUploadDamageTests.cpp
    CompositorViewporterTests.cpp
    CompositorVtSwitchShortcutTests.cpp
    CompositorWindowFrameGeometryTests.cpp
    CompositorWindowGeometryTests.cpp
    CompositorXdgOutputTests.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/BufferRelease.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/DmabufFeedback.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/DmabufValidation.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/LayerShellState.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/LayerShellZones.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/SeatSerials.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/Wayland/SubsurfaceState.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/PresenterEnv.cpp)

  if(LAMBDAUI_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
    list(APPEND _lambda_desktop_test_sources CompositorWindowStateTests.cpp)
  endif()
  if(LAMBDAUI_PLATFORM_LINUX_KMS)
    list(APPEND _lambda_desktop_test_sources
      ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager/Compositor/SceneGraph/CompositorSceneGraph.cpp)
  endif()
endif()

add_executable(lambda_desktop_tests ${_lambda_desktop_test_sources})
target_include_directories(lambda_desktop_tests PRIVATE
  ${LAMBDAUI_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDAUI_FRAMEWORK_SOURCE_DIR}
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-editor
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-files
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-settings
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-terminal
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-window-manager
  ${LAMBDAUI_FRAMEWORK_PROTOCOL_DIR}/wayland)
target_link_libraries(lambda_desktop_tests PRIVATE
  lambda_desktop_system
  tomlplusplus::tomlplusplus
  doctest::doctest
  util)

if(LAMBDAUI_PLATFORM_LINUX_WAYLAND OR LAMBDAUI_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  target_link_libraries(lambda_desktop_tests PRIVATE
    PkgConfig::VULKAN
    PkgConfig::LIBDRM
    PkgConfig::ZLIB)
  if(LAMBDAUI_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
    add_dependencies(lambda_desktop_tests lambda_wayland_server_protocols)
    target_link_libraries(lambda_desktop_tests PRIVATE PkgConfig::WAYLAND_SERVER)
  endif()
  if(DEFINED LAMBDA_WAYLAND_PROTOCOL_DIR)
    target_include_directories(lambda_desktop_tests PRIVATE ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  endif()
  pkg_check_modules(LIBPULSE QUIET IMPORTED_TARGET libpulse)
  if(LIBPULSE_FOUND)
    target_compile_definitions(lambda_desktop_tests PRIVATE LAMBDA_SHELL_HAVE_LIBPULSE=1)
    target_link_libraries(lambda_desktop_tests PRIVATE PkgConfig::LIBPULSE)
  endif()
endif()

target_compile_definitions(lambda_desktop_tests PRIVATE LAMBDAUI_TESTING=1)
if(LAMBDAUI_PLATFORM_LINUX_KMS)
  target_compile_definitions(lambda_desktop_tests PRIVATE LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH=1)
endif()
set_target_properties(lambda_desktop_tests PROPERTIES CXX_STANDARD 23)
if(LAMBDAUI_ENABLE_ASAN)
  target_compile_options(lambda_desktop_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_desktop_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_desktop_tests COMMAND lambda_desktop_tests)
