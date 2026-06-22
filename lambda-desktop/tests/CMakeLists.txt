set(_lambda_desktop_test_sources
  BlueZTests.cpp
  DBusTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-editor/EditorCommands.cpp
  EditorCommandsTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-editor/EditorDocument.cpp
  EditorDocumentTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-editor/EditorFileWatcher.cpp
  EditorFileWatcherTests.cpp
  FilesFlowGridTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-files/FilesStore.cpp
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
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-settings/SettingsBackend.cpp
  SettingsBackendTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellAppRegistry.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellAudioControl.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellConnection.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellIpc.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellJson.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellModel.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellModels.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/ShellSystemStatus.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/UI/LambdaDock.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell/Shell/UI/LambdaShellTypes.cpp
  ShellAppRegistryTests.cpp
  ShellAudioControlTests.cpp
  ShellConnectionTests.cpp
  ShellDockTests.cpp
  ShellIpcTests.cpp
  ShellModelTests.cpp
  ShellModelsTests.cpp
  ShellSystemStatusTests.cpp
  StatusNotifierWatcherTests.cpp
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-terminal/TerminalCore.cpp
  TerminalCoreTests.cpp
  TerminalPtySmokeTests.cpp
  UDisks2Tests.cpp
  UPowerTests.cpp)

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  list(APPEND _lambda_desktop_test_sources
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Chrome/WindowChromeRenderer.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Config/CompositorConfig.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/SceneDamage.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Screenshot.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Surface/CommittedSurfacePainter.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Window/WindowGeometry.cpp
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
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/BufferRelease.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/DmabufFeedback.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/DmabufValidation.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/LayerShellState.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/LayerShellZones.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/SeatSerials.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/SubsurfaceState.cpp
    ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/PresenterEnv.cpp)

  if(LAMBDA_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
    list(APPEND _lambda_desktop_test_sources CompositorWindowStateTests.cpp)
  endif()
  if(LAMBDA_PLATFORM_LINUX_KMS)
    list(APPEND _lambda_desktop_test_sources
      ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager/Compositor/SceneGraph/CompositorSceneGraph.cpp)
  endif()
endif()

add_executable(lambda_desktop_tests ${_lambda_desktop_test_sources})
target_include_directories(lambda_desktop_tests PRIVATE
  ${LAMBDA_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDA_FRAMEWORK_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-editor
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-files
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-settings
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-shell
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-terminal
  ${PROJECT_SOURCE_DIR}/lambda-desktop/apps/lambda-window-manager
  ${LAMBDA_FRAMEWORK_PROTOCOL_DIR}/wayland)
target_link_libraries(lambda_desktop_tests PRIVATE
  lambda_desktop_system
  tomlplusplus::tomlplusplus
  doctest::doctest
  util)

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  target_link_libraries(lambda_desktop_tests PRIVATE
    PkgConfig::VULKAN
    PkgConfig::LIBDRM
    PkgConfig::ZLIB)
  if(LAMBDA_PLATFORM_LINUX_KMS OR LAMBDA_BUILD_WINDOW_MANAGER)
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

target_compile_definitions(lambda_desktop_tests PRIVATE LAMBDA_TESTING=1)
if(LAMBDA_PLATFORM_LINUX_KMS)
  target_compile_definitions(lambda_desktop_tests PRIVATE LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH=1)
endif()
set_target_properties(lambda_desktop_tests PROPERTIES CXX_STANDARD 23)
if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda_desktop_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda_desktop_tests PRIVATE -fsanitize=address)
endif()
add_test(NAME lambda_desktop_tests COMMAND lambda_desktop_tests)
