cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.0")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(flux VERSION 5.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_RELEASE_FLAGS "-march=native -O3 -flto")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxApp.cmake)

set(FLUX_PLATFORM "AUTO" CACHE STRING
    "Flux backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS.")
set_property(CACHE FLUX_PLATFORM PROPERTY STRINGS AUTO MACOS LINUX_WAYLAND LINUX_KMS)

if(FLUX_PLATFORM STREQUAL "AUTO")
  if(APPLE)
    set(FLUX_PLATFORM_MACOS ON)
    set(FLUX_PLATFORM "MACOS" CACHE STRING
      "Flux backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS." FORCE)
  elseif(UNIX)
    set(FLUX_PLATFORM_LINUX_WAYLAND ON)
    set(FLUX_PLATFORM "LINUX_WAYLAND" CACHE STRING
      "Flux backend: AUTO, MACOS, LINUX_WAYLAND, LINUX_KMS." FORCE)
  else()
    message(FATAL_ERROR "Flux: no default backend for this host.")
  endif()
elseif(FLUX_PLATFORM STREQUAL "MACOS")
  if(NOT APPLE)
    message(FATAL_ERROR "FLUX_PLATFORM=MACOS requires an Apple host.")
  endif()
  set(FLUX_PLATFORM_MACOS ON)
elseif(FLUX_PLATFORM STREQUAL "LINUX_WAYLAND")
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "FLUX_PLATFORM=LINUX_WAYLAND requires a Linux/Unix host.")
  endif()
  set(FLUX_PLATFORM_LINUX_WAYLAND ON)
elseif(FLUX_PLATFORM STREQUAL "LINUX_KMS")
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "FLUX_PLATFORM=LINUX_KMS requires a Linux host.")
  endif()
  set(FLUX_PLATFORM_LINUX_KMS ON)
else()
  message(FATAL_ERROR "Flux: FLUX_PLATFORM=${FLUX_PLATFORM} is not implemented yet.")
endif()

if(FLUX_PLATFORM_MACOS)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

include(FetchContent)
# libtess2 — path fill/stroke tessellation (same stack as upstream flux)
FetchContent_Declare(libtess2
  GIT_REPOSITORY https://github.com/memononen/libtess2.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libtess2)
if(NOT TARGET libtess2)
  add_library(libtess2 STATIC
    ${libtess2_SOURCE_DIR}/Source/bucketalloc.c
    ${libtess2_SOURCE_DIR}/Source/dict.c
    ${libtess2_SOURCE_DIR}/Source/geom.c
    ${libtess2_SOURCE_DIR}/Source/mesh.c
    ${libtess2_SOURCE_DIR}/Source/priorityq.c
    ${libtess2_SOURCE_DIR}/Source/sweep.c
    ${libtess2_SOURCE_DIR}/Source/tess.c
  )
  set_target_properties(libtess2 PROPERTIES LINKER_LANGUAGE C)
endif()
set_target_properties(libtess2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
target_include_directories(libtess2 PUBLIC
  $<BUILD_INTERFACE:${libtess2_SOURCE_DIR}/Include>
)

FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

option(FLUX_ENABLE_DEFAULT_EVENT_LOGGING
  "Print built-in Application event handlers to stdout (disable for quiet or embedded builds)."
  OFF)

option(FLUX_ENABLE_ASAN
  "Build flux and flux_tests with AddressSanitizer instrumentation."
  OFF)

option(FLUX_PROFILE_REACTIVE
  "Compile deterministic reactive profiling counters and 5-second display-link reports."
  OFF)

option(FLUX_V5_PROTOTYPE
  "Build the standalone Flux v5 reactive prototype."
  OFF)

set(LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT OFF)
if(UNIX AND NOT APPLE)
  set(LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT ON)
endif()
option(LAMBDA_BUILD_WINDOW_MANAGER
  "Build the Linux KMS lambda-window-manager executable alongside the selected Flux app platform."
  ${LAMBDA_BUILD_WINDOW_MANAGER_DEFAULT})

if(FLUX_V5_PROTOTYPE)
  enable_testing()
  add_subdirectory(prototype/reactive)
endif()

if(FLUX_PLATFORM_MACOS)
  # Offline Metal: compile CanvasShaders.metal → air → metallib → C header (xxd) embedded in MetalShaderLibrary.mm
  set(FLUX_METAL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Metal/CanvasShaders.metal)
  set(FLUX_METAL_AIR ${CMAKE_CURRENT_BINARY_DIR}/CanvasShaders.air)
  set(FLUX_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/FluxShaders.metallib)
  set(FLUX_METALLIB_EMBED_H ${CMAKE_CURRENT_BINARY_DIR}/FluxShaders.metallib.h)
  # Run xxd in the build dir with basename only so symbols are FluxShaders_metallib (not full path).
  add_custom_command(
    OUTPUT ${FLUX_METAL_AIR} ${FLUX_METALLIB} ${FLUX_METALLIB_EMBED_H}
    COMMAND xcrun -sdk macosx metal -mmacosx-version-min=11.0 -std=metal3.0 -c ${FLUX_METAL_SOURCE} -o ${FLUX_METAL_AIR}
    COMMAND xcrun -sdk macosx metallib ${FLUX_METAL_AIR} -o ${FLUX_METALLIB}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}
            xxd -i FluxShaders.metallib FluxShaders.metallib.h
    DEPENDS ${FLUX_METAL_SOURCE}
    COMMENT "Compile CanvasShaders.metal to FluxShaders.metallib (embedded)"
    VERBATIM
  )
  add_custom_target(flux_shaders DEPENDS ${FLUX_METALLIB_EMBED_H})
endif()

set(FLUX_CORE_SOURCES
  src/Core/Identity.cpp
)

set(FLUX_REACTIVE_SOURCES
  src/Reactive/Observer.cpp
  src/Reactive/Easing.cpp
  src/Reactive/Transition.cpp
  src/Reactive/AnimationClock.cpp
)

set(FLUX_GRAPHICS_SOURCES
  src/Graphics/Styles.cpp
  src/Graphics/AttributedString.cpp
  src/Graphics/Canvas.cpp
  src/Graphics/ImageLoader.cpp
  src/Graphics/RenderTarget.cpp
  src/Graphics/TextCacheDebugOverlay.cpp
  src/Graphics/TextSystem.cpp
  src/Graphics/xxhash_impl.c
  src/Graphics/Path.cpp
  src/Graphics/PathFlattener.cpp
  src/Graphics/SvgPath.cpp
)

set(FLUX_SCENEGRAPH_SOURCES
  src/SceneGraph/SceneNode.cpp
  src/SceneGraph/RasterCacheNode.cpp
  src/SceneGraph/RectNode.cpp
  src/SceneGraph/TextNode.cpp
  src/SceneGraph/ImageNode.cpp
  src/SceneGraph/PathNode.cpp
  src/SceneGraph/RenderNode.cpp
  src/SceneGraph/RenderTargetScene.cpp
  src/SceneGraph/SceneGraph.cpp
  src/SceneGraph/SceneInteraction.cpp
  src/SceneGraph/SceneRenderer.cpp
)

set(FLUX_LAYOUT_SOURCES
  src/Layout/Algorithms/StackLayout.cpp
)

set(FLUX_UI_SOURCES
  src/UI/EventQueue.cpp
  src/UI/MenuRoleDefaults.cpp
  src/UI/Window.cpp
  src/UI/WindowRender.cpp
  src/UI/MeasureContext.cpp
  src/UI/ActionRegistry.cpp
  src/UI/InputFieldChrome.cpp
  src/UI/InputFieldLayout.cpp
  src/UI/Detail/TraversalContext.cpp
  src/UI/Environment.cpp
  src/UI/Theme.cpp
  src/UI/Element/ElementMeasurement.cpp
  src/UI/Element/ElementModifiers.cpp
  src/UI/Element/ElementTypeErasure.cpp
  src/UI/LayoutDebugDump.cpp
  src/UI/ViewLayout/ScrollLayout.cpp
  src/UI/ViewLayout/OverlayLayout.cpp
  src/UI/MountContext.cpp
  src/UI/MountRoot.cpp
  src/UI/Runtime.cpp
  src/UI/TransientPopoverHost.cpp
  src/UI/Overlay.cpp
  src/UI/PopupMenu.cpp
  src/UI/OverlaySurfaceHelpers.cpp
  src/UI/Views/Alert.cpp
  src/UI/Views/BackdropBlur.cpp
  src/UI/Views/Badge.cpp
  src/UI/Views/Button.cpp
  src/UI/Views/Card.cpp
  src/UI/Views/Checkbox.cpp
  src/UI/Views/Dialog.cpp
  src/UI/Views/Divider.cpp
  src/UI/Views/Grid.cpp
  src/UI/Views/Icon.cpp
  src/UI/Views/Image.cpp
  src/UI/Views/ListView.cpp
  src/UI/Views/Rectangle.cpp
  src/UI/Views/Render.cpp
  src/UI/Views/Text.cpp
  src/UI/Views/TextEditUtils.cpp
  src/UI/Views/TextInput.cpp
  src/UI/Views/LayoutContainers.cpp
  src/UI/Views/Popover.cpp
  src/UI/Views/PopoverCalloutPath.cpp
  src/UI/Views/PopoverCalloutShape.cpp
  src/UI/Views/ProgressBar.cpp
  src/UI/Views/ScrollView.cpp
  src/UI/Views/Select.cpp
  src/UI/Views/SegmentedControl.cpp
  src/UI/Views/Slider.cpp
  src/UI/Views/Svg.cpp
  src/UI/Views/TableView.cpp
  src/UI/Views/Toast.cpp
  src/UI/Views/Tooltip.cpp
  src/UI/Views/TransformViews.cpp
  src/UI/Views/Toggle.cpp
  src/Shell/ShellIpc.cpp
)

add_library(flux STATIC
  ${FLUX_CORE_SOURCES}
  ${FLUX_REACTIVE_SOURCES}
  ${FLUX_GRAPHICS_SOURCES}
  ${FLUX_SCENEGRAPH_SOURCES}
  ${FLUX_LAYOUT_SOURCES}
  ${FLUX_UI_SOURCES}
)

add_custom_target(flux_module_dependency_check
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_dependencies.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Check Flux module dependencies"
  VERBATIM
)

if(FLUX_PLATFORM_MACOS)
  target_sources(flux PRIVATE
    src/UI/Application.mm
    src/Graphics/CoreTextSystem.mm
    src/Graphics/Metal/MetalFrameRecorder.mm
    src/Graphics/Metal/GlyphAtlas.mm
    src/Graphics/Metal/MetalImage.mm
    src/Graphics/Metal/MetalRenderTarget.mm
    src/Graphics/Metal/MetalPathRasterizer.cpp
    src/Graphics/Metal/MetalShaderLibrary.mm
    src/Graphics/Metal/MetalDeviceResources.mm
    src/Graphics/Metal/MetalCanvas.mm
    src/Platform/Mac/MacApplication.mm
    src/Platform/Mac/MacMetalWindow.mm
    src/Platform/Mac/MacClipboard.mm
  )
  add_dependencies(flux flux_shaders)
elseif(FLUX_PLATFORM_LINUX_WAYLAND)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxWaylandProtocols.cmake)
  find_package(PkgConfig REQUIRED)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  if(NOT WAYLAND_PROTOCOLS_DIR)
    message(FATAL_ERROR
      "Flux: pkg-config package wayland-protocols did not provide pkgdatadir. "
      "Install/reinstall wayland-protocols or fix PKG_CONFIG_PATH.")
  endif()
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  if(LAMBDA_BUILD_WINDOW_MANAGER)
    pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
    pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
    pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
  endif()
  find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
  set(FLUX_VULKAN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/Graphics/Vulkan/generated)
  file(MAKE_DIRECTORY ${FLUX_VULKAN_GENERATED_DIR})
  flux_setup_wayland_protocol_paths("${WAYLAND_PROTOCOLS_DIR}")
  foreach(_flux_wayland_protocol_xml IN ITEMS
      "${FLUX_XDG_SHELL_XML}"
      "${FLUX_XDG_DECORATION_XML}"
      "${FLUX_XDG_OUTPUT_XML}"
      "${FLUX_XDG_ACTIVATION_XML}"
      "${FLUX_VIEWPORTER_XML}"
      "${FLUX_PRESENTATION_TIME_XML}"
      "${FLUX_LINUX_DMABUF_XML}"
      "${FLUX_CURSOR_SHAPE_XML}"
      "${FLUX_IDLE_INHIBIT_XML}"
      "${FLUX_POINTER_CONSTRAINTS_XML}"
      "${FLUX_RELATIVE_POINTER_XML}"
      "${FLUX_PRIMARY_SELECTION_XML}"
      "${FLUX_FRACTIONAL_SCALE_XML}"
      "${FLUX_XX_CUTOUTS_XML}"
      "${FLUX_WLR_LAYER_SHELL_XML}"
      "${FLUX_EXT_BACKGROUND_EFFECT_XML}")
    if(NOT EXISTS "${_flux_wayland_protocol_xml}")
      message(FATAL_ERROR
        "Flux: required Wayland protocol XML not found: ${_flux_wayland_protocol_xml}. "
        "Install the wayland-protocols development package or fix pkg-config's wayland-protocols path.")
    endif()
  endforeach()
  add_library(flux_wayland_client_protocols OBJECT
    src/Compositor/Protocols/tablet-v2-tool-interface-stub.c)
  target_include_directories(flux_wayland_client_protocols PUBLIC ${FLUX_WAYLAND_PROTOCOL_DIR})
  flux_wayland_client_protocols(flux_wayland_client_protocols
    "${FLUX_XDG_SHELL_XML}|xdg-shell"
    "${FLUX_XDG_DECORATION_XML}|xdg-decoration-unstable-v1"
    "${FLUX_VIEWPORTER_XML}|viewporter"
    "${FLUX_FRACTIONAL_SCALE_XML}|fractional-scale-v1"
    "${FLUX_XX_CUTOUTS_XML}|xx-cutouts-v1"
    "${FLUX_WLR_LAYER_SHELL_XML}|wlr-layer-shell-unstable-v1"
    "${FLUX_EXT_BACKGROUND_EFFECT_XML}|ext-background-effect-v1"
    "${FLUX_LINUX_DMABUF_XML}|linux-dmabuf-unstable-v1"
    "${FLUX_CURSOR_SHAPE_XML}|cursor-shape-v1"
    "${FLUX_IDLE_INHIBIT_XML}|idle-inhibit-unstable-v1"
    "${FLUX_PRESENTATION_TIME_XML}|presentation-time"
    "${FLUX_RELATIVE_POINTER_XML}|relative-pointer-unstable-v1"
    "${FLUX_POINTER_CONSTRAINTS_XML}|pointer-constraints-unstable-v1"
    "${FLUX_PRIMARY_SELECTION_XML}|primary-selection-unstable-v1"
    "${FLUX_XDG_ACTIVATION_XML}|xdg-activation-v1")
  if(LAMBDA_BUILD_WINDOW_MANAGER)
    add_library(flux_wayland_server_protocols OBJECT)
    target_include_directories(flux_wayland_server_protocols PUBLIC ${FLUX_WAYLAND_PROTOCOL_DIR})
    flux_wayland_server_protocols(flux_wayland_server_protocols
      "${FLUX_XDG_SHELL_XML}|xdg-shell"
      "${FLUX_XDG_DECORATION_XML}|xdg-decoration-unstable-v1"
      "${FLUX_XDG_OUTPUT_XML}|xdg-output-unstable-v1"
      "${FLUX_VIEWPORTER_XML}|viewporter"
      "${FLUX_FRACTIONAL_SCALE_XML}|fractional-scale-v1"
      "${FLUX_WLR_LAYER_SHELL_XML}|wlr-layer-shell-unstable-v1"
      "${FLUX_PRESENTATION_TIME_XML}|presentation-time"
      "${FLUX_RELATIVE_POINTER_XML}|relative-pointer-unstable-v1"
      "${FLUX_POINTER_CONSTRAINTS_XML}|pointer-constraints-unstable-v1"
      "${FLUX_PRIMARY_SELECTION_XML}|primary-selection-unstable-v1"
      "${FLUX_LINUX_DMABUF_XML}|linux-dmabuf-unstable-v1"
      "${FLUX_CURSOR_SHAPE_XML}|cursor-shape-v1"
      "${FLUX_IDLE_INHIBIT_XML}|idle-inhibit-unstable-v1"
      "${FLUX_XDG_ACTIVATION_XML}|xdg-activation-v1"
      "${FLUX_EXT_BACKGROUND_EFFECT_XML}|ext-background-effect-v1"
      "${FLUX_XX_CUTOUTS_XML}|xx-cutouts-v1")
  endif()
  set(_flux_vulkan_shader_headers)
  foreach(_flux_shader rect.vert rect.frag image.vert image.frag image_unpremultiply.frag backdrop.frag backdrop_blur.frag path.vert path.frag)
    string(REPLACE "." "_" _flux_shader_name ${_flux_shader})
    set(_flux_shader_src ${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Vulkan/shaders/${_flux_shader})
    set(_flux_shader_spv ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/${_flux_shader}.spv)
    set(_flux_shader_header ${FLUX_VULKAN_GENERATED_DIR}/${_flux_shader_name}_spv.hpp)
    add_custom_command(
      OUTPUT ${_flux_shader_header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders
      COMMAND ${GLSLANG_VALIDATOR} -V ${_flux_shader_src} -o ${_flux_shader_spv}
      COMMAND ${CMAKE_COMMAND}
              -DINPUT=${_flux_shader_spv}
              -DOUTPUT=${_flux_shader_header}
              -DSYMBOL=flux_${_flux_shader_name}_spv
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxEmbedBinary.cmake
      DEPENDS ${_flux_shader_src} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxEmbedBinary.cmake
      VERBATIM
    )
    list(APPEND _flux_vulkan_shader_headers ${_flux_shader_header})
  endforeach()
  add_custom_target(flux_vulkan_shaders DEPENDS ${_flux_vulkan_shader_headers})
  add_custom_target(flux_vulkan_shaders_check
    DEPENDS ${_flux_vulkan_shader_headers}
    COMMENT "Regenerating Vulkan SPIR-V headers from shader sources"
    VERBATIM
  )
  add_dependencies(flux flux_vulkan_shaders)
  target_sources(flux PRIVATE
    src/UI/Application.cpp
    src/Graphics/Linux/FreeTypeTextSystem.cpp
    src/Graphics/Vulkan/VulkanContext.cpp
    src/Graphics/Vulkan/VulkanFrameRecorder.cpp
    src/Graphics/Vulkan/VulkanGpuCanvas.cpp
    src/Graphics/Vulkan/VulkanRenderTarget.cpp
    src/Platform/Linux/Common/XkbState.cpp
    src/Platform/Linux/WaylandApplication.cpp
    src/Platform/Linux/WaylandWindow.cpp
  )
  target_link_libraries(flux PRIVATE flux_wayland_client_protocols)
elseif(FLUX_PLATFORM_LINUX_KMS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxWaylandProtocols.cmake)
  find_package(PkgConfig REQUIRED)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  if(NOT WAYLAND_PROTOCOLS_DIR)
    message(FATAL_ERROR
      "Flux: pkg-config package wayland-protocols did not provide pkgdatadir. "
      "Install/reinstall wayland-protocols or fix PKG_CONFIG_PATH.")
  endif()
  flux_setup_wayland_protocol_paths("${WAYLAND_PROTOCOLS_DIR}")
  add_library(flux_wayland_server_protocols OBJECT)
  target_include_directories(flux_wayland_server_protocols PUBLIC ${FLUX_WAYLAND_PROTOCOL_DIR})
  flux_wayland_server_protocols(flux_wayland_server_protocols
    "${FLUX_XDG_SHELL_XML}|xdg-shell"
    "${FLUX_XDG_DECORATION_XML}|xdg-decoration-unstable-v1"
    "${FLUX_XDG_OUTPUT_XML}|xdg-output-unstable-v1"
    "${FLUX_VIEWPORTER_XML}|viewporter"
    "${FLUX_FRACTIONAL_SCALE_XML}|fractional-scale-v1"
    "${FLUX_WLR_LAYER_SHELL_XML}|wlr-layer-shell-unstable-v1"
    "${FLUX_PRESENTATION_TIME_XML}|presentation-time"
    "${FLUX_RELATIVE_POINTER_XML}|relative-pointer-unstable-v1"
    "${FLUX_POINTER_CONSTRAINTS_XML}|pointer-constraints-unstable-v1"
    "${FLUX_PRIMARY_SELECTION_XML}|primary-selection-unstable-v1"
    "${FLUX_LINUX_DMABUF_XML}|linux-dmabuf-unstable-v1"
    "${FLUX_CURSOR_SHAPE_XML}|cursor-shape-v1"
    "${FLUX_IDLE_INHIBIT_XML}|idle-inhibit-unstable-v1"
    "${FLUX_XDG_ACTIVATION_XML}|xdg-activation-v1"
    "${FLUX_EXT_BACKGROUND_EFFECT_XML}|ext-background-effect-v1"
    "${FLUX_XX_CUTOUTS_XML}|xx-cutouts-v1")
  pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
  pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
  pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
  pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
  pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
  pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
  pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
  set(FLUX_VULKAN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/Graphics/Vulkan/generated)
  file(MAKE_DIRECTORY ${FLUX_VULKAN_GENERATED_DIR})
  set(_flux_vulkan_shader_headers)
  foreach(_flux_shader rect.vert rect.frag image.vert image.frag image_unpremultiply.frag backdrop.frag backdrop_blur.frag path.vert path.frag)
    string(REPLACE "." "_" _flux_shader_name ${_flux_shader})
    set(_flux_shader_src ${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Vulkan/shaders/${_flux_shader})
    set(_flux_shader_spv ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/${_flux_shader}.spv)
    set(_flux_shader_header ${FLUX_VULKAN_GENERATED_DIR}/${_flux_shader_name}_spv.hpp)
    add_custom_command(
      OUTPUT ${_flux_shader_header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders
      COMMAND ${GLSLANG_VALIDATOR} -V ${_flux_shader_src} -o ${_flux_shader_spv}
      COMMAND ${CMAKE_COMMAND}
              -DINPUT=${_flux_shader_spv}
              -DOUTPUT=${_flux_shader_header}
              -DSYMBOL=flux_${_flux_shader_name}_spv
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxEmbedBinary.cmake
      DEPENDS ${_flux_shader_src} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluxEmbedBinary.cmake
      VERBATIM
    )
    list(APPEND _flux_vulkan_shader_headers ${_flux_shader_header})
  endforeach()
  add_custom_target(flux_vulkan_shaders DEPENDS ${_flux_vulkan_shader_headers})
  add_custom_target(flux_vulkan_shaders_check
    DEPENDS ${_flux_vulkan_shader_headers}
    COMMENT "Regenerating Vulkan SPIR-V headers from shader sources"
    VERBATIM
  )
  add_dependencies(flux flux_vulkan_shaders)
  target_include_directories(flux PRIVATE ${FLUX_WAYLAND_PROTOCOL_DIR})
  target_sources(flux PRIVATE
    src/UI/Application.cpp
    src/Graphics/Linux/FreeTypeTextSystem.cpp
    src/Graphics/Vulkan/VulkanContext.cpp
    src/Graphics/Vulkan/VulkanFrameRecorder.cpp
    src/Graphics/Vulkan/VulkanGpuCanvas.cpp
    src/Graphics/Vulkan/VulkanRenderTarget.cpp
    src/Platform/Linux/Common/XkbState.cpp
    src/Platform/Linux/KmsApplication.cpp
    src/Platform/Linux/KmsWindow.cpp
    src/Platform/Linux/KmsOutput.cpp
    src/Platform/Linux/KmsInput.cpp
    src/Compositor/Protocols/tablet-v2-tool-interface-stub.c
  )
  target_link_libraries(flux PRIVATE flux_wayland_server_protocols)
endif()

target_include_directories(flux PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(flux PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

target_compile_options(flux PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

if(APPLE)
  target_compile_options(flux PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()

if(FLUX_ENABLE_ASAN)
  target_compile_options(flux PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(flux PRIVATE -fsanitize=address)
endif()

if(FLUX_ENABLE_DEFAULT_EVENT_LOGGING)
  target_compile_definitions(flux PRIVATE FLUX_ENABLE_DEFAULT_EVENT_LOGGING=1)
else()
  target_compile_definitions(flux PRIVATE FLUX_ENABLE_DEFAULT_EVENT_LOGGING=0)
endif()

if(FLUX_PROFILE_REACTIVE)
  target_compile_definitions(flux PUBLIC FLUX_PROFILE_REACTIVE=1)
else()
  target_compile_definitions(flux PUBLIC FLUX_PROFILE_REACTIVE=0)
endif()

option(FLUX_DISABLE_VARIANT_REFS
  "Deep-copy paragraph cache layouts (revert to clone on fast path); disables variant shared_ptr retention."
  OFF)
if(FLUX_DISABLE_VARIANT_REFS)
  target_compile_definitions(flux PRIVATE FLUX_DISABLE_VARIANT_REFS=1)
endif()

option(FLUX_ENABLE_PARAGRAPH_CACHE_PARALLEL_ASSERT
  "Debug: compare incremental vs full paragraph-cache assembly (non-NDEBUG only)."
  OFF)
if(FLUX_ENABLE_PARAGRAPH_CACHE_PARALLEL_ASSERT)
  target_compile_definitions(flux PRIVATE FLUX_PARAGRAPH_CACHE_PARALLEL_ASSERT=1)
endif()

target_link_libraries(flux PRIVATE libtess2)

add_library(lambda-shell-ui STATIC
  src/Shell/ShellController.cpp
  src/Shell/ShellConnection.cpp
  src/Shell/ShellJson.cpp
  src/Shell/ShellModel.cpp
  src/Shell/UI/LambdaCommandLauncher.cpp
  src/Shell/UI/LambdaDock.cpp
  src/Shell/UI/LambdaShellTypes.cpp
  src/Shell/UI/LambdaTopBar.cpp
)
target_include_directories(lambda-shell-ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lambda-shell-ui PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(lambda-shell-ui PUBLIC flux)

if(FLUX_PLATFORM_MACOS)
  target_compile_definitions(flux
    PUBLIC FLUX_METAL=1 FLUX_VULKAN=0
    PRIVATE FLUX_PLATFORM_MACOS=1)
  target_link_libraries(flux PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework Metal"
    "-framework MetalKit"
    "-framework Foundation"
    "-framework CoreText"
    "-framework CoreVideo"
  )
elseif(FLUX_PLATFORM_LINUX_WAYLAND)
  target_compile_definitions(flux
    PUBLIC FLUX_METAL=0 FLUX_VULKAN=1
    PRIVATE FLUX_PLATFORM_LINUX_WAYLAND=1)
  target_include_directories(flux PRIVATE ${FLUX_WAYLAND_PROTOCOL_DIR})
  target_link_libraries(flux PRIVATE
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::WAYLAND_CURSOR
    PkgConfig::LIBDRM
    PkgConfig::XKBCOMMON
    PkgConfig::VULKAN
    PkgConfig::FREETYPE
    PkgConfig::FONTCONFIG
    PkgConfig::HARFBUZZ
    PkgConfig::ZLIB
    ${CMAKE_DL_LIBS})
elseif(FLUX_PLATFORM_LINUX_KMS)
  target_compile_definitions(flux
    PUBLIC FLUX_METAL=0 FLUX_VULKAN=1
    PRIVATE FLUX_PLATFORM_LINUX_KMS=1)
  target_include_directories(flux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols)
  target_link_libraries(flux PRIVATE
    PkgConfig::LIBDRM
    PkgConfig::GBM
    PkgConfig::LIBINPUT
    PkgConfig::LIBUDEV
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    PkgConfig::VULKAN
    PkgConfig::FREETYPE
    PkgConfig::FONTCONFIG
    PkgConfig::HARFBUZZ
    PkgConfig::ZLIB
    ${CMAKE_DL_LIBS})
endif()

if(FLUX_PLATFORM_MACOS)
  add_executable(lambda-window-manager-render-fixture
    src/Compositor/Chrome/WindowChromeRenderer.cpp
    src/Compositor/Surface/CommittedSurfacePainter.cpp
    src/Compositor/Tools/RenderWindowChromeFixture.mm
  )
  target_include_directories(lambda-window-manager-render-fixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lambda-window-manager-render-fixture PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(lambda-window-manager-render-fixture PRIVATE
    flux
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework ImageIO"
    "-framework Metal")
endif()

if(FLUX_PLATFORM_LINUX_WAYLAND OR FLUX_PLATFORM_LINUX_KMS)
  add_executable(lambda-window-manager-render-fixture
    src/Compositor/Chrome/WindowChromeRenderer.cpp
    src/Compositor/Surface/CommittedSurfacePainter.cpp
    src/Compositor/Tools/RenderWindowChromeFixture.cpp
  )
  target_include_directories(lambda-window-manager-render-fixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lambda-window-manager-render-fixture PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(lambda-window-manager-render-fixture PRIVATE
    flux
    PkgConfig::VULKAN
    PkgConfig::ZLIB)
endif()

if(LAMBDA_BUILD_WINDOW_MANAGER)
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "LAMBDA_BUILD_WINDOW_MANAGER is only supported on Linux.")
  endif()
  add_executable(lambda-window-manager
    src/Compositor/Chrome/CursorRenderer.cpp
    src/Compositor/Chrome/WindowChromeRenderer.cpp
    src/Compositor/CompositorRuntime.cpp
    src/Compositor/CompositorConfigWatch.cpp
    src/Compositor/CompositorRenderFrame.cpp
    src/Compositor/PresentationLoop.cpp
    src/Compositor/Presenter.cpp
    src/Compositor/PresenterEnv.cpp
    src/Compositor/Screenshot.cpp
    src/Compositor/Config/AppliedCompositorConfig.cpp
    src/Compositor/Config/WallpaperLoader.cpp
    src/Compositor/Config/WallpaperCache.cpp
    src/Compositor/Config/CompositorConfig.cpp
    src/Compositor/Diagnostics/CrashLog.cpp
    src/Compositor/Diagnostics/CpuTrace.cpp
    src/Compositor/Input/KmsInputBridge.cpp
    src/Compositor/Surface/CommittedSurfacePainter.cpp
    src/Compositor/Surface/SurfaceRenderer.cpp
    src/Compositor/Shell/ShellProtocol.cpp
    src/Compositor/Wayland/Globals/Activation.cpp
    src/Compositor/Wayland/Globals/BackgroundEffect.cpp
    src/Compositor/Wayland/Globals/Core.cpp
    src/Compositor/Wayland/Globals/CursorShape.cpp
    src/Compositor/Wayland/Globals/Cutouts.cpp
    src/Compositor/Wayland/Globals/FractionalScale.cpp
    src/Compositor/Wayland/Globals/IdleInhibit.cpp
    src/Compositor/Wayland/Globals/LayerShell.cpp
    src/Compositor/Wayland/Globals/LinuxDmabuf.cpp
    src/Compositor/Wayland/Globals/Output.cpp
    src/Compositor/Wayland/Globals/PointerExtensions.cpp
    src/Compositor/Wayland/Globals/Presentation.cpp
    src/Compositor/Wayland/Globals/Seat.cpp
    src/Compositor/Wayland/Globals/Selection.cpp
    src/Compositor/Wayland/Globals/Shm.cpp
    src/Compositor/Wayland/Globals/Viewporter.cpp
    src/Compositor/Wayland/Globals/XdgOutput.cpp
    src/Compositor/Wayland/Globals/XdgShell.cpp
    src/Compositor/Wayland/Destroy.cpp
    src/Compositor/Wayland/FrameScheduler.cpp
    src/Compositor/Wayland/ServerLifecycle.cpp
    src/Compositor/Wayland/Snapshots.cpp
    src/Compositor/Window/WindowGeometry.cpp
    src/Compositor/Window/WindowManager.cpp
    src/Compositor/Window/WindowManagerCommon.cpp
    src/Compositor/Window/PointerRouter.cpp
    src/Compositor/Window/FocusStack.cpp
    src/Compositor/Window/KeyboardShortcuts.cpp
    src/Compositor/Window/InteractiveMoveResize.cpp
    src/Compositor/Window/LayerShellInput.cpp
    src/Compositor/main.cpp
    src/Compositor/WaylandServer.cpp
    src/Compositor/Protocols/tablet-v2-tool-interface-stub.c
    src/Platform/Linux/KmsApplication.cpp
    src/Platform/Linux/KmsInput.cpp
    src/Platform/Linux/KmsOutput.cpp
    src/Platform/Linux/KmsWindow.cpp
  )
  target_include_directories(lambda-window-manager PRIVATE
    ${FLUX_WAYLAND_PROTOCOL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lambda-window-manager PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(lambda-window-manager PRIVATE
    flux
    flux_wayland_server_protocols
    tomlplusplus::tomlplusplus
    PkgConfig::WAYLAND_SERVER
    PkgConfig::LIBDRM
    PkgConfig::GBM
    PkgConfig::LIBINPUT
    PkgConfig::LIBUDEV
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::XKBCOMMON
    PkgConfig::VULKAN
    PkgConfig::FREETYPE
    PkgConfig::FONTCONFIG
    PkgConfig::HARFBUZZ
    PkgConfig::ZLIB
    ${CMAKE_DL_LIBS})
  if(UNIX AND NOT APPLE)
    target_link_options(lambda-window-manager PRIVATE -rdynamic)
  endif()

  add_executable(lambda-shell
    src/Shell/LambdaShell.cpp
  )
  target_include_directories(lambda-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lambda-shell PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(lambda-shell PRIVATE
    lambda-shell-ui
    flux
  )
  add_custom_command(TARGET lambda-shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lambda-shell>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/MaterialSymbolsRounded.ttf"
      "$<TARGET_FILE_DIR:lambda-shell>/fonts/MaterialSymbolsRounded.ttf")

  function(flux_link_compositor_demo TARGET)
    target_include_directories(${TARGET} PRIVATE ${FLUX_WAYLAND_PROTOCOL_DIR})
    target_compile_options(${TARGET} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
    )
    target_link_libraries(${TARGET} PRIVATE
      flux_wayland_client_protocols
      PkgConfig::WAYLAND_CLIENT)
  endfunction()

  add_executable(lambda-window-manager-shm-demo
    src/Compositor/ShmDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-shm-demo)

  add_executable(lambda-window-manager-dmabuf-demo
    src/Compositor/DmabufDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-dmabuf-demo)
  target_link_libraries(lambda-window-manager-dmabuf-demo PRIVATE
    PkgConfig::GBM
    PkgConfig::LIBDRM)

  add_executable(lambda-window-manager-viewport-demo
    src/Compositor/ViewportDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-viewport-demo)

  add_executable(lambda-window-manager-fractional-scale-demo
    src/Compositor/FractionalScaleDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-fractional-scale-demo)

  add_executable(lambda-window-manager-cursor-shape-demo
    src/Compositor/CursorShapeDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-cursor-shape-demo)

  add_executable(lambda-window-manager-idle-inhibit-demo
    src/Compositor/IdleInhibitDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-idle-inhibit-demo)

  add_executable(lambda-window-manager-layer-shell-demo
    src/Compositor/LayerShellDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-layer-shell-demo)

  add_executable(lambda-window-manager-presentation-time-demo
    src/Compositor/PresentationTimeDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-presentation-time-demo)

  add_executable(lambda-window-manager-relative-pointer-demo
    src/Compositor/RelativePointerDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-relative-pointer-demo)

  add_executable(lambda-window-manager-pointer-constraints-demo
    src/Compositor/PointerConstraintsDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-pointer-constraints-demo)

  add_executable(lambda-window-manager-primary-selection-demo
    src/Compositor/PrimarySelectionDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-primary-selection-demo)

  add_executable(lambda-window-manager-clipboard-demo
    src/Compositor/ClipboardDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-clipboard-demo)

  add_executable(lambda-window-manager-dnd-demo
    src/Compositor/DragDropDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-dnd-demo)

  add_executable(lambda-window-manager-popup-demo
    src/Compositor/PopupDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-popup-demo)

  add_executable(lambda-window-manager-popup-grab-demo
    src/Compositor/PopupGrabDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-popup-grab-demo)

  add_executable(lambda-window-manager-activation-demo
    src/Compositor/ActivationDemoClient.cpp
  )
  flux_link_compositor_demo(lambda-window-manager-activation-demo)

endif()

option(FLUX_BUILD_TESTS
  "Build unit and integration tests under tests/."
  OFF)

if(FLUX_BUILD_TESTS)
  target_compile_definitions(flux PRIVATE FLUX_TESTING=1)

  # doctest's CMakeLists may require a minimum policy on CMake 4+.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
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

  enable_testing()

  set(_flux_test_sources
    tests/AlertTests.cpp
    tests/AnimationTests.cpp
    tests/BindableInferenceTests.cpp
    tests/EnvironmentKeyOtherTU.cpp
    tests/EnvironmentKeyTests.cpp
    tests/FilesFlowGridTests.cpp
    examples/lambda-files/FilesStore.cpp
    tests/ForTests.cpp
    tests/MountRootTests.cpp
    tests/main.cpp
    tests/PathFlattenerTests.cpp
    tests/SceneGraphTests.cpp
    tests/ParagraphCacheTests.cpp
    tests/ParagraphCacheIncrementalTests.cpp
    tests/RuntimeInputTests.cpp
    tests/RuntimeTests.cpp
    tests/ShowTests.cpp
    tests/ShellIpcTests.cpp
    tests/SvgPathTests.cpp
    tests/ThemeReactivityTests.cpp
  )
  if(FLUX_PLATFORM_MACOS)
    list(APPEND _flux_test_sources tests/MetalCanvasTests.mm)
  endif()
  if(FLUX_PLATFORM_LINUX_WAYLAND OR FLUX_PLATFORM_LINUX_KMS)
    list(APPEND _flux_test_sources
      src/Compositor/Chrome/WindowChromeRenderer.cpp
      src/Compositor/Config/CompositorConfig.cpp
      src/Compositor/Surface/CommittedSurfacePainter.cpp
      src/Compositor/Window/WindowGeometry.cpp
      tests/CompositorConfigTests.cpp
      tests/CompositorDecorationStateTests.cpp
      tests/CompositorWindowGeometryTests.cpp
      tests/CompositorConfigHotReloadTests.cpp
      tests/CompositorLayerShellZoneTests.cpp
      tests/CompositorPresentationFeedbackTests.cpp
      tests/CompositorSnapshotBuilderTests.cpp
      tests/CompositorSubsurfaceHitTests.cpp
      src/Compositor/Wayland/LayerShellZones.cpp
      src/Compositor/PresenterEnv.cpp
      tests/VulkanRenderTargetTests.cpp
    )
  endif()
  add_executable(flux_tests ${_flux_test_sources})

  target_include_directories(flux_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/lambda-files
  )

  target_link_libraries(flux_tests PRIVATE
    flux
    tomlplusplus::tomlplusplus
    doctest::doctest
  )
  if(FLUX_PLATFORM_LINUX_WAYLAND OR FLUX_PLATFORM_LINUX_KMS)
    target_link_libraries(flux_tests PRIVATE PkgConfig::VULKAN)
  endif()
  target_compile_definitions(flux_tests PRIVATE FLUX_TESTING=1)

  set_target_properties(flux_tests PROPERTIES CXX_STANDARD 23)

  if(FLUX_ENABLE_ASAN)
    target_compile_options(flux_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(flux_tests PRIVATE -fsanitize=address)
  endif()

  add_test(NAME flux_tests COMMAND flux_tests)

  add_executable(flux_reactive_tests
    tests/Reactive/main.cpp
    tests/Reactive/SignalTests.cpp
    tests/Reactive/ComputedTests.cpp
    tests/Reactive/EffectTests.cpp
    tests/Reactive/ScopeTests.cpp
    tests/Reactive/DiamondTests.cpp
    tests/Reactive/BindableTests.cpp
  )

  target_include_directories(flux_reactive_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(flux_reactive_tests PRIVATE
    flux
    doctest::doctest
  )

  set_target_properties(flux_reactive_tests PROPERTIES CXX_STANDARD 23)

  if(FLUX_ENABLE_ASAN)
    target_compile_options(flux_reactive_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(flux_reactive_tests PRIVATE -fsanitize=address)
  endif()

  add_test(NAME flux_reactive_tests COMMAND flux_reactive_tests)
endif()

if(FLUX_PLATFORM_LINUX_KMS)
  add_executable(kms-vulkan-smoke tools/kms-vulkan-smoke.cpp)
  target_link_libraries(kms-vulkan-smoke PRIVATE flux)
  set_target_properties(kms-vulkan-smoke PROPERTIES CXX_STANDARD 23)
endif()

option(FLUX_BUILD_EXAMPLES
  "Build standalone examples under examples/."
  OFF)

option(FLUX_BUILD_SOLITAIRE_APP
  "Build the Solitaire app."
  ON)

if(FLUX_BUILD_SOLITAIRE_APP)
  add_subdirectory(examples/solitaire-app)
endif()

if(FLUX_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

option(FLUX_BUILD_BENCHMARKS "Build micro-benchmarks in bench/." OFF)

if(FLUX_BUILD_BENCHMARKS)
  add_executable(paragraph_cache_bench
    bench/paragraph_cache/bench.cpp
  )
  target_include_directories(paragraph_cache_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_link_libraries(paragraph_cache_bench PRIVATE flux)
  set_target_properties(paragraph_cache_bench PROPERTIES CXX_STANDARD 23)

endif()
