if(NOT DEFINED LAMBDA_FRAMEWORK_DIR)
  get_filename_component(LAMBDA_FRAMEWORK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  set(LAMBDA_FRAMEWORK_INCLUDE_DIR "${LAMBDA_FRAMEWORK_DIR}/include")
  set(LAMBDA_FRAMEWORK_SOURCE_DIR "${LAMBDA_FRAMEWORK_DIR}/src")
  set(LAMBDA_FRAMEWORK_CMAKE_DIR "${LAMBDA_FRAMEWORK_DIR}/cmake")
  set(LAMBDA_FRAMEWORK_RESOURCE_DIR "${LAMBDA_FRAMEWORK_DIR}/resources")
  set(LAMBDA_FRAMEWORK_PROTOCOL_DIR "${LAMBDA_FRAMEWORK_DIR}/protocols")
  set(LAMBDA_FRAMEWORK_VENDOR_DIR "${LAMBDA_FRAMEWORK_DIR}/vendor")
endif()

set(LAMBDA_CORE_SOURCES
  Core/Identity.cpp
)

set(LAMBDA_REACTIVE_SOURCES
  Reactive/Observer.cpp
  Reactive/Easing.cpp
  Reactive/Transition.cpp
  Reactive/AnimationClock.cpp
)

set(LAMBDA_GRAPHICS_SOURCES
  Graphics/Styles.cpp
  Graphics/AttributedString.cpp
  Graphics/Canvas.cpp
  Graphics/ImageLoader.cpp
  Graphics/RenderTarget.cpp
  Graphics/TextCacheDebugOverlay.cpp
  Graphics/TextSystem.cpp
  Graphics/xxhash_impl.c
  Graphics/Path.cpp
  Graphics/PathFlattener.cpp
  Graphics/SvgPath.cpp
)

set(LAMBDA_SCENEGRAPH_SOURCES
  SceneGraph/SceneNode.cpp
  SceneGraph/RasterCacheNode.cpp
  SceneGraph/RectNode.cpp
  SceneGraph/TextNode.cpp
  SceneGraph/ImageNode.cpp
  SceneGraph/PathNode.cpp
  SceneGraph/RenderNode.cpp
  SceneGraph/RenderTargetScene.cpp
  SceneGraph/SceneGraph.cpp
  SceneGraph/SceneInteraction.cpp
  SceneGraph/SceneRenderer.cpp
)

set(LAMBDA_LAYOUT_SOURCES
  Layout/Algorithms/StackLayout.cpp
)

set(LAMBDA_UI_SOURCES
  UI/EventQueue.cpp
  UI/MenuRoleDefaults.cpp
  UI/Window.cpp
  UI/WindowRender.cpp
  UI/MeasureContext.cpp
  UI/CommandRegistry.cpp
  UI/InputFieldChrome.cpp
  UI/InputFieldLayout.cpp
  UI/Detail/TraversalContext.cpp
  UI/Environment.cpp
  UI/Theme.cpp
  UI/Element/ElementMeasurement.cpp
  UI/Element/ElementModifiers.cpp
  UI/Element/ElementTypeErasure.cpp
  UI/LayoutDebugDump.cpp
  UI/ViewLayout/ScrollLayout.cpp
  UI/ViewLayout/OverlayLayout.cpp
  UI/MountContext.cpp
  UI/MountRoot.cpp
  UI/Runtime.cpp
  UI/TransientPopoverHost.cpp
  UI/Overlay.cpp
  UI/PopupMenu.cpp
  UI/OverlaySurfaceHelpers.cpp
  UI/Views/Alert.cpp
  UI/Views/BackdropBlur.cpp
  UI/Views/Badge.cpp
  UI/Views/Button.cpp
  UI/Views/Card.cpp
  UI/Views/Checkbox.cpp
  UI/Views/Dialog.cpp
  UI/Views/Divider.cpp
  UI/Views/FileDialog.cpp
  UI/Views/Grid.cpp
  UI/Views/Icon.cpp
  UI/Views/Image.cpp
  UI/Views/ListView.cpp
  UI/Views/Rectangle.cpp
  UI/Views/Render.cpp
  UI/Views/Text.cpp
  UI/Views/TextEditUtils.cpp
  UI/Views/TextInput.cpp
  UI/Views/LayoutContainers.cpp
  UI/Views/Popover.cpp
  UI/Views/PopoverCalloutPath.cpp
  UI/Views/PopoverCalloutShape.cpp
  UI/Views/ProgressBar.cpp
  UI/Views/ScrollView.cpp
  UI/Views/Select.cpp
  UI/Views/SegmentedControl.cpp
  UI/Views/Slider.cpp
  UI/Views/Svg.cpp
  UI/Views/TableView.cpp
  UI/Views/Toast.cpp
  UI/Views/Tooltip.cpp
  UI/Views/TransformViews.cpp
  UI/Views/Toggle.cpp
)

add_library(lambda SHARED
  ${LAMBDA_CORE_SOURCES}
  ${LAMBDA_REACTIVE_SOURCES}
  ${LAMBDA_GRAPHICS_SOURCES}
  ${LAMBDA_SCENEGRAPH_SOURCES}
  ${LAMBDA_LAYOUT_SOURCES}
  ${LAMBDA_UI_SOURCES}
)
add_library(lambda::lambda ALIAS lambda)

set_target_properties(lambda PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_custom_target(lambda_module_dependency_check
  COMMAND ${LAMBDA_FRAMEWORK_DIR}/scripts/check_module_dependencies.sh
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Check Lambda module dependencies"
  VERBATIM
)

if(LAMBDA_PLATFORM_MACOS)
  set(LAMBDA_METAL_SOURCE ${LAMBDA_FRAMEWORK_SOURCE_DIR}/Graphics/Metal/CanvasShaders.metal)
  set(LAMBDA_METAL_AIR ${CMAKE_CURRENT_BINARY_DIR}/CanvasShaders.air)
  set(LAMBDA_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/LambdaShaders.metallib)
  set(LAMBDA_METALLIB_EMBED_H ${CMAKE_CURRENT_BINARY_DIR}/LambdaShaders.metallib.h)
  add_custom_command(
    OUTPUT ${LAMBDA_METAL_AIR} ${LAMBDA_METALLIB} ${LAMBDA_METALLIB_EMBED_H}
    COMMAND xcrun -sdk macosx metal -mmacosx-version-min=11.0 -std=metal3.0 -c ${LAMBDA_METAL_SOURCE} -o ${LAMBDA_METAL_AIR}
    COMMAND xcrun -sdk macosx metallib ${LAMBDA_METAL_AIR} -o ${LAMBDA_METALLIB}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}
            xxd -i LambdaShaders.metallib LambdaShaders.metallib.h
    DEPENDS ${LAMBDA_METAL_SOURCE}
    COMMENT "Compile CanvasShaders.metal to LambdaShaders.metallib (embedded)"
    VERBATIM
  )
  add_custom_target(lambda_shaders DEPENDS ${LAMBDA_METALLIB_EMBED_H})
  add_dependencies(lambda lambda_shaders)
  target_sources(lambda PRIVATE
    UI/Application.mm
    Graphics/CoreTextSystem.mm
    Graphics/Metal/MetalFrameRecorder.mm
    Graphics/Metal/GlyphAtlas.mm
    Graphics/Metal/MetalImage.mm
    Graphics/Metal/MetalRenderTarget.mm
    Graphics/Metal/MetalPathRasterizer.cpp
    Graphics/Metal/MetalShaderLibrary.mm
    Graphics/Metal/MetalDeviceResources.mm
    Graphics/Metal/MetalCanvas.mm
    Platform/Mac/MacApplication.mm
    Platform/Mac/MacMetalWindow.mm
    Platform/Mac/MacClipboard.mm
  )
elseif(LAMBDA_PLATFORM_LINUX_WAYLAND)
  include(${LAMBDA_FRAMEWORK_CMAKE_DIR}/LambdaWaylandProtocols.cmake)
  find_package(PkgConfig REQUIRED)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  if(NOT WAYLAND_PROTOCOLS_DIR)
    message(FATAL_ERROR "Lambda: wayland-protocols did not provide pkgdatadir.")
  endif()
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
  pkg_check_modules(LIBRSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
  lambda_setup_wayland_protocol_paths("${WAYLAND_PROTOCOLS_DIR}")
  add_library(lambda_wayland_client_protocols OBJECT
    ${LAMBDA_FRAMEWORK_PROTOCOL_DIR}/wayland/tablet-v2-tool-interface-stub.c)
  target_include_directories(lambda_wayland_client_protocols PUBLIC ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  lambda_wayland_client_protocols(lambda_wayland_client_protocols
    "${LAMBDA_XDG_SHELL_XML}|xdg-shell"
    "${LAMBDA_XDG_DECORATION_XML}|xdg-decoration-unstable-v1"
    "${LAMBDA_VIEWPORTER_XML}|viewporter"
    "${LAMBDA_FRACTIONAL_SCALE_XML}|fractional-scale-v1"
    "${LAMBDA_XX_CUTOUTS_XML}|xx-cutouts-v1"
    "${LAMBDA_WLR_LAYER_SHELL_XML}|wlr-layer-shell-unstable-v1"
    "${LAMBDA_EXT_BACKGROUND_EFFECT_XML}|ext-background-effect-v1"
    "${LAMBDA_LINUX_DMABUF_XML}|linux-dmabuf-unstable-v1"
    "${LAMBDA_CURSOR_SHAPE_XML}|cursor-shape-v1"
    "${LAMBDA_IDLE_INHIBIT_XML}|idle-inhibit-unstable-v1"
    "${LAMBDA_PRESENTATION_TIME_XML}|presentation-time"
    "${LAMBDA_RELATIVE_POINTER_XML}|relative-pointer-unstable-v1"
    "${LAMBDA_POINTER_CONSTRAINTS_XML}|pointer-constraints-unstable-v1"
    "${LAMBDA_PRIMARY_SELECTION_XML}|primary-selection-unstable-v1"
    "${LAMBDA_XDG_ACTIVATION_XML}|xdg-activation-v1")
  target_sources(lambda PRIVATE
    UI/Application.cpp
    Graphics/Linux/FreeTypeTextSystem.cpp
    Graphics/Vulkan/VulkanAllocator.cpp
    Graphics/Vulkan/VulkanContext.cpp
    Graphics/Vulkan/VulkanFrameRecorder.cpp
    Graphics/Vulkan/VulkanGpuCanvas.cpp
    Graphics/Vulkan/VulkanRenderTarget.cpp
    Platform/Linux/Common/XkbState.cpp
    Platform/Linux/WaylandApplication.cpp
    Platform/Linux/WaylandWindow.cpp
  )
  target_link_libraries(lambda PRIVATE lambda_wayland_client_protocols)
elseif(LAMBDA_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
  pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
  pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
  pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
  pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
  pkg_check_modules(LIBRSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
  target_sources(lambda PRIVATE
    UI/Application.cpp
    Graphics/Linux/FreeTypeTextSystem.cpp
    Graphics/Vulkan/VulkanAllocator.cpp
    Graphics/Vulkan/VulkanContext.cpp
    Graphics/Vulkan/VulkanFrameRecorder.cpp
    Graphics/Vulkan/VulkanGpuCanvas.cpp
    Graphics/Vulkan/VulkanRenderTarget.cpp
    Platform/Linux/Common/XkbState.cpp
    Platform/Linux/KmsApplication.cpp
    Platform/Linux/KmsWindow.cpp
    Platform/Linux/KmsOutput.cpp
    Platform/Linux/KmsInput.cpp
  )
endif()

if(LAMBDA_PLATFORM_LINUX_WAYLAND OR LAMBDA_PLATFORM_LINUX_KMS)
  set(LAMBDA_VULKAN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/Graphics/Vulkan/generated)
  file(MAKE_DIRECTORY ${LAMBDA_VULKAN_GENERATED_DIR})
  set(_lambda_vulkan_shader_headers)
  foreach(_lambda_shader rect.vert rect.frag callout.vert callout.frag image.vert image.frag image_unpremultiply.frag backdrop.frag backdrop_blur.frag path.vert path.frag)
    string(REPLACE "." "_" _lambda_shader_name ${_lambda_shader})
    set(_lambda_shader_src ${LAMBDA_FRAMEWORK_SOURCE_DIR}/Graphics/Vulkan/shaders/${_lambda_shader})
    set(_lambda_shader_spv ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/${_lambda_shader}.spv)
    set(_lambda_shader_header ${LAMBDA_VULKAN_GENERATED_DIR}/${_lambda_shader_name}_spv.hpp)
    add_custom_command(
      OUTPUT ${_lambda_shader_header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders
      COMMAND ${GLSLANG_VALIDATOR} -V ${_lambda_shader_src} -o ${_lambda_shader_spv}
      COMMAND ${CMAKE_COMMAND}
              -DINPUT=${_lambda_shader_spv}
              -DOUTPUT=${_lambda_shader_header}
              -DSYMBOL=lambda_${_lambda_shader_name}_spv
              -P ${LAMBDA_FRAMEWORK_CMAKE_DIR}/LambdaEmbedBinary.cmake
      DEPENDS ${_lambda_shader_src} ${LAMBDA_FRAMEWORK_CMAKE_DIR}/LambdaEmbedBinary.cmake
      VERBATIM
    )
    list(APPEND _lambda_vulkan_shader_headers ${_lambda_shader_header})
  endforeach()
  add_custom_target(lambda_vulkan_shaders DEPENDS ${_lambda_vulkan_shader_headers})
  add_custom_target(lambda_vulkan_shaders_check
    DEPENDS ${_lambda_vulkan_shader_headers}
    COMMENT "Regenerating Vulkan SPIR-V headers from shader sources"
    VERBATIM
  )
  add_dependencies(lambda lambda_vulkan_shaders)
endif()

target_include_directories(lambda PUBLIC
  ${LAMBDA_FRAMEWORK_INCLUDE_DIR}
)
target_include_directories(lambda PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${LAMBDA_FRAMEWORK_SOURCE_DIR}
  ${LAMBDA_FRAMEWORK_VENDOR_DIR}
)

target_compile_options(lambda PRIVATE -Wall -Wextra -Wpedantic)

if(APPLE)
  target_compile_options(lambda PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()

if(LAMBDA_ENABLE_ASAN)
  target_compile_options(lambda PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lambda PRIVATE -fsanitize=address)
endif()

target_compile_definitions(lambda PRIVATE
  LAMBDA_ENABLE_DEFAULT_EVENT_LOGGING=$<BOOL:${LAMBDA_ENABLE_DEFAULT_EVENT_LOGGING}>
)

if(LAMBDA_PROFILE_REACTIVE)
  target_compile_definitions(lambda PUBLIC LAMBDA_PROFILE_REACTIVE=1)
else()
  target_compile_definitions(lambda PUBLIC LAMBDA_PROFILE_REACTIVE=0)
endif()

if(LAMBDA_DISABLE_VARIANT_REFS)
  target_compile_definitions(lambda PRIVATE LAMBDA_DISABLE_VARIANT_REFS=1)
endif()

if(LAMBDA_ENABLE_PARAGRAPH_CACHE_PARALLEL_ASSERT)
  target_compile_definitions(lambda PRIVATE LAMBDA_PARAGRAPH_CACHE_PARALLEL_ASSERT=1)
endif()

if(LAMBDA_PLATFORM_MACOS)
  target_compile_definitions(lambda PUBLIC LAMBDA_METAL=1 LAMBDA_VULKAN=0 PRIVATE LAMBDA_PLATFORM_MACOS=1)
  target_link_libraries(lambda PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework Metal"
    "-framework MetalKit"
    "-framework Foundation"
    "-framework CoreText"
    "-framework CoreVideo")
elseif(LAMBDA_PLATFORM_LINUX_WAYLAND)
  target_compile_definitions(lambda PUBLIC LAMBDA_METAL=0 LAMBDA_VULKAN=1 PRIVATE LAMBDA_PLATFORM_LINUX_WAYLAND=1)
  target_include_directories(lambda PRIVATE ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  target_link_libraries(lambda PRIVATE
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::WAYLAND_CURSOR
    PkgConfig::LIBDRM
    PkgConfig::XKBCOMMON
    PkgConfig::VULKAN
    PkgConfig::FREETYPE
    PkgConfig::FONTCONFIG
    PkgConfig::HARFBUZZ
    PkgConfig::LIBRSVG
    PkgConfig::ZLIB
    ${CMAKE_DL_LIBS})
elseif(LAMBDA_PLATFORM_LINUX_KMS)
  target_compile_definitions(lambda PUBLIC LAMBDA_METAL=0 LAMBDA_VULKAN=1 PRIVATE LAMBDA_PLATFORM_LINUX_KMS=1)
  target_link_libraries(lambda PRIVATE
    PkgConfig::LIBDRM
    PkgConfig::GBM
    PkgConfig::LIBINPUT
    PkgConfig::LIBSEAT
    PkgConfig::LIBUDEV
    PkgConfig::XKBCOMMON
    PkgConfig::VULKAN
    PkgConfig::FREETYPE
    PkgConfig::FONTCONFIG
    PkgConfig::HARFBUZZ
    PkgConfig::LIBRSVG
    PkgConfig::ZLIB
    ${CMAKE_DL_LIBS})
endif()

target_link_libraries(lambda PRIVATE libtess2)

install(TARGETS lambda
  EXPORT LambdaTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
