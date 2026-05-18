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
  src/UI/Overlay.cpp
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
  find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
  set(FLUX_WAYLAND_PROTOCOL_DIR ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols)
  set(FLUX_VULKAN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/Graphics/Vulkan/generated)
  file(MAKE_DIRECTORY ${FLUX_WAYLAND_PROTOCOL_DIR})
  file(MAKE_DIRECTORY ${FLUX_VULKAN_GENERATED_DIR})
  set(FLUX_XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
  set(FLUX_XDG_DECORATION_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
  set(FLUX_VIEWPORTER_XML "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
  foreach(_flux_wayland_protocol_xml IN ITEMS
      "${FLUX_XDG_SHELL_XML}"
      "${FLUX_XDG_DECORATION_XML}"
      "${FLUX_VIEWPORTER_XML}")
    if(NOT EXISTS "${_flux_wayland_protocol_xml}")
      message(FATAL_ERROR
        "Flux: required Wayland protocol XML not found: ${_flux_wayland_protocol_xml}. "
        "Install the wayland-protocols development package or fix pkg-config's wayland-protocols path.")
    endif()
  endforeach()
  add_custom_command(
    OUTPUT
      ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-shell-client-protocol.h
      ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-shell-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${FLUX_XDG_SHELL_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-shell-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${FLUX_XDG_SHELL_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-shell-protocol.c
    DEPENDS ${FLUX_XDG_SHELL_XML}
    VERBATIM
  )
  add_custom_command(
    OUTPUT
      ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-decoration-unstable-v1-client-protocol.h
      ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-decoration-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${FLUX_XDG_DECORATION_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-decoration-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${FLUX_XDG_DECORATION_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-decoration-unstable-v1-protocol.c
    DEPENDS ${FLUX_XDG_DECORATION_XML}
    VERBATIM
  )
  add_custom_command(
    OUTPUT
      ${FLUX_WAYLAND_PROTOCOL_DIR}/viewporter-client-protocol.h
      ${FLUX_WAYLAND_PROTOCOL_DIR}/viewporter-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${FLUX_VIEWPORTER_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/viewporter-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${FLUX_VIEWPORTER_XML}
            ${FLUX_WAYLAND_PROTOCOL_DIR}/viewporter-protocol.c
    DEPENDS ${FLUX_VIEWPORTER_XML}
    VERBATIM
  )
  set(_flux_vulkan_shader_headers)
  foreach(_flux_shader rect.vert rect.frag image.vert image.frag backdrop.frag backdrop_blur.frag path.vert path.frag)
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
    ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-shell-protocol.c
    ${FLUX_WAYLAND_PROTOCOL_DIR}/xdg-decoration-unstable-v1-protocol.c
    ${FLUX_WAYLAND_PROTOCOL_DIR}/viewporter-protocol.c
  )
elseif(FLUX_PLATFORM_LINUX_KMS)
  find_package(PkgConfig REQUIRED)
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
  foreach(_flux_shader rect.vert rect.frag image.vert image.frag backdrop.frag backdrop_blur.frag path.vert path.frag)
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
    src/Platform/Linux/KmsApplication.cpp
    src/Platform/Linux/KmsWindow.cpp
    src/Platform/Linux/KmsOutput.cpp
    src/Platform/Linux/KmsInput.cpp
    src/Compositor/Protocols/cursor-shape-v1-protocol.c
    src/Compositor/Protocols/idle-inhibit-unstable-v1-protocol.c
    src/Compositor/Protocols/tablet-v2-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
    src/Compositor/Protocols/xdg-decoration-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-output-unstable-v1-protocol.c
    src/Compositor/Protocols/viewporter-protocol.c
    src/Compositor/Protocols/fractional-scale-v1-protocol.c
    src/Compositor/Protocols/wlr-layer-shell-unstable-v1-protocol.c
    src/Compositor/Protocols/presentation-time-protocol.c
    src/Compositor/Protocols/relative-pointer-unstable-v1-protocol.c
    src/Compositor/Protocols/pointer-constraints-unstable-v1-protocol.c
    src/Compositor/Protocols/primary-selection-unstable-v1-protocol.c
    src/Compositor/Protocols/linux-dmabuf-unstable-v1-protocol.c
  )
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

option(FLUX_BUILD_COMPOSITOR
  "Build the phase-1 flux-compositor executable. Requires FLUX_PLATFORM=LINUX_KMS."
  OFF)

if(FLUX_BUILD_COMPOSITOR)
  if(NOT FLUX_PLATFORM_LINUX_KMS)
    message(FATAL_ERROR "FLUX_BUILD_COMPOSITOR requires FLUX_PLATFORM=LINUX_KMS.")
  endif()
  add_executable(flux-compositor
    src/Compositor/Chrome/CursorRenderer.cpp
    src/Compositor/Chrome/WindowChromeRenderer.cpp
    src/Compositor/Config/CompositorConfig.cpp
    src/Compositor/Input/KmsInputBridge.cpp
    src/Compositor/Surface/SurfaceRenderer.cpp
    src/Compositor/Window/WindowGeometry.cpp
    src/Compositor/main.cpp
    src/Compositor/WaylandServer.cpp
  )
  target_include_directories(flux-compositor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor PRIVATE flux PkgConfig::WAYLAND_SERVER PkgConfig::LIBDRM)

  add_executable(flux-compositor-shm-demo
    src/Compositor/ShmDemoClient.cpp
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-shm-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-shm-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-shm-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-dmabuf-demo
    src/Compositor/DmabufDemoClient.cpp
    src/Compositor/Protocols/xdg-shell-protocol.c
    src/Compositor/Protocols/linux-dmabuf-unstable-v1-protocol.c
  )
  target_include_directories(flux-compositor-dmabuf-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-dmabuf-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-dmabuf-demo PRIVATE
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::GBM
    PkgConfig::LIBDRM)

  add_executable(flux-compositor-viewport-demo
    src/Compositor/ViewportDemoClient.cpp
    src/Compositor/Protocols/xdg-shell-protocol.c
    src/Compositor/Protocols/viewporter-protocol.c
  )
  target_include_directories(flux-compositor-viewport-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-viewport-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-viewport-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-fractional-scale-demo
    src/Compositor/FractionalScaleDemoClient.cpp
    src/Compositor/Protocols/fractional-scale-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-fractional-scale-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-fractional-scale-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-fractional-scale-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-cursor-shape-demo
    src/Compositor/CursorShapeDemoClient.cpp
    src/Compositor/Protocols/cursor-shape-v1-protocol.c
    src/Compositor/Protocols/tablet-v2-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-cursor-shape-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-cursor-shape-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-cursor-shape-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-idle-inhibit-demo
    src/Compositor/IdleInhibitDemoClient.cpp
    src/Compositor/Protocols/idle-inhibit-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-idle-inhibit-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-idle-inhibit-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-idle-inhibit-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-layer-shell-demo
    src/Compositor/LayerShellDemoClient.cpp
    src/Compositor/Protocols/wlr-layer-shell-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-layer-shell-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-layer-shell-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-layer-shell-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-presentation-time-demo
    src/Compositor/PresentationTimeDemoClient.cpp
    src/Compositor/Protocols/presentation-time-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-presentation-time-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-presentation-time-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-presentation-time-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-relative-pointer-demo
    src/Compositor/RelativePointerDemoClient.cpp
    src/Compositor/Protocols/relative-pointer-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-relative-pointer-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-relative-pointer-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-relative-pointer-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-pointer-constraints-demo
    src/Compositor/PointerConstraintsDemoClient.cpp
    src/Compositor/Protocols/pointer-constraints-unstable-v1-protocol.c
    src/Compositor/Protocols/relative-pointer-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-pointer-constraints-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-pointer-constraints-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-pointer-constraints-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-primary-selection-demo
    src/Compositor/PrimarySelectionDemoClient.cpp
    src/Compositor/Protocols/primary-selection-unstable-v1-protocol.c
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-primary-selection-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-primary-selection-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-primary-selection-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-clipboard-demo
    src/Compositor/ClipboardDemoClient.cpp
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-clipboard-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-clipboard-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-clipboard-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

  add_executable(flux-compositor-dnd-demo
    src/Compositor/DragDropDemoClient.cpp
    src/Compositor/Protocols/xdg-shell-protocol.c
  )
  target_include_directories(flux-compositor-dnd-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Compositor/Protocols
  )
  target_compile_options(flux-compositor-dnd-demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
  target_link_libraries(flux-compositor-dnd-demo PRIVATE PkgConfig::WAYLAND_CLIENT)

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
    tests/SvgPathTests.cpp
    tests/ThemeReactivityTests.cpp
  )
  if(FLUX_PLATFORM_MACOS)
    list(APPEND _flux_test_sources tests/MetalCanvasTests.mm)
  endif()
  if(FLUX_PLATFORM_LINUX_WAYLAND OR FLUX_PLATFORM_LINUX_KMS)
    list(APPEND _flux_test_sources
      src/Compositor/Config/CompositorConfig.cpp
      src/Compositor/Window/WindowGeometry.cpp
      tests/CompositorConfigTests.cpp
      tests/CompositorWindowGeometryTests.cpp
      tests/VulkanRenderTargetTests.cpp
    )
  endif()
  add_executable(flux_tests ${_flux_test_sources})

  target_include_directories(flux_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(flux_tests PRIVATE
    flux
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
