# Flux v5

Flux is a small **C++23** application framework for **macOS** with a **Metal** 2D canvas, Linux **Wayland/Vulkan**, and Linux **KMS/DRM/Vulkan** backends, vector paths tessellated through [libtess2](https://github.com/memononen/libtess2), platform text layout, and a retained declarative UI layer.

The v5 UI runtime mounts each view tree once, owns reactive state in scopes, and updates retained scene nodes through `Signal`, `Computed`, `Effect`, `Bindable`, animation handles, and reactive environment values.

## Build

Requirements: **CMake 3.25+**. macOS builds need **Xcode command-line tools** (`xcrun metal` and `xxd`). Linux Wayland builds need `wayland-client`, `wayland-cursor`, `wayland-protocols`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `zlib`, and `glslangValidator`. Linux KMS builds need `libdrm`, `libinput`, `libudev`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `zlib`, and `glslangValidator`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLUX_BUILD_TESTS=ON -DFLUX_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

By default only the static `flux` library is built. Pass `-DFLUX_BUILD_EXAMPLES=ON` to build the sample executables.

## Examples

Current example targets:

`alert-demo`, `animation-demo`, `blend-demo`, `button-demo`, `card-demo`, `checkbox-demo`, `cursor-demo`, `hello-world`, `icon-demo`, `image-demo`, `lambda-editor`, `lambda-preview`, `lambda-studio`, `layout-demo`, `markdown-formatter-demo`, `popover-demo`, `reactive-demo`, `scene-graph-demo`, `scroll-demo`, `segmented-demo`, `select-demo`, `slider-demo`, `table-demo`, `text-demo`, `textinput-demo`, `theme-demo`, `toast-demo`, `toggle-demo`, `tooltip-demo`, `typography-demo`.

## Options

- `FLUX_PLATFORM` - `AUTO` (default), `MACOS`, `LINUX_WAYLAND`, or `LINUX_KMS`.
- `FLUX_BUILD_TESTS` - default `OFF`; set `ON` to build unit and reactive tests.
- `FLUX_BUILD_EXAMPLES` - default `OFF`; set `ON` to build examples.
- `FLUX_BUILD_BENCHMARKS` - default `OFF`; set `ON` to build micro-benchmarks.
- `FLUX_ENABLE_ASAN` - default `OFF`; set `ON` for AddressSanitizer builds.
- `FLUX_ENABLE_DEFAULT_EVENT_LOGGING` - default `OFF`; set `ON` to print default `Application` event handlers.
- `FLUX_V5_PROTOTYPE` - default `OFF`; builds the archived standalone prototype used during the v5 migration.

## Documentation

- [Documentation index](docs/README.md)
- [Project status and roadmap](docs/roadmap.md)
- [Conventions](docs/conventions.md)
- [Reactive graph](docs/reactive-graph.md)
- [Migrating to v5](docs/migrating-to-v5.md)
- [Compositor](docs/compositor.md) (Linux window manager)

Public API headers live under `include/Flux/`. The umbrella header is [`include/Flux.hpp`](include/Flux.hpp). Declarative UI apps usually include [`include/Flux/UI/UI.hpp`](include/Flux/UI/UI.hpp) and use `Window::setView`.
