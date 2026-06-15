# Lambda Desktop

Lambda Desktop is a small **C++23** desktop framework and app suite for **macOS** with a **Metal** 2D canvas, Linux **Wayland/Vulkan**, and Linux **KMS/DRM/Vulkan** backends, vector paths tessellated through [libtess2](https://github.com/memononen/libtess2), platform text layout, and a retained declarative UI layer.

The v5 UI runtime mounts each view tree once, owns reactive state in scopes, and updates retained scene nodes through `Signal`, `Computed`, `Effect`, `Bindable`, animation handles, and reactive environment values.

## Build

Requirements: **CMake 3.25+**.

macOS builds need **full Xcode selected as the active developer directory**, because Lambda compiles and embeds Metal shaders with `xcrun -sdk macosx metal`, `xcrun -sdk macosx metallib`, and `xxd`. Xcode Command Line Tools alone may configure a C++ compiler but still omit the Metal shader compiler. Install Xcode, then run:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
xcodebuild -downloadComponent MetalToolchain
xcrun -sdk macosx -find metal
xcrun -sdk macosx -find metallib
```

Linux Wayland builds need `wayland-client`, `wayland-cursor`, `wayland-protocols`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `zlib`, and `glslangValidator`. Linux KMS builds need `libdrm`, `libinput`, `libudev`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `zlib`, and `glslangValidator`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_BUILD_TESTS=ON -DLAMBDA_BUILD_DEMOS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

By default the shared `lambda` library and real apps under `apps/` are built. Pass `-DLAMBDA_BUILD_DEMOS=ON` to build the demo executables under `demos/`.

## Apps

Current app targets:

`lambda-editor`, `lambda-files`, `lambda-preview`, `lambda-settings`, `lambda-shell`, `lambda-terminal`, `solitaire-app`, and on Linux `lambda-window-manager`.

## Demos

Current demo targets:

`alert-demo`, `animation-demo`, `blend-demo`, `button-demo`, `card-demo`, `checkbox-demo`, `clock-demo`, `cursor-demo`, `gradient-demo`, `hello-world`, `icon-demo`, `image-demo`, `layout-demo`, `markdown-formatter-demo`, `output-demo`, `popover-demo`, `scene-graph-demo`, `scroll-demo`, `segmented-demo`, `select-demo`, `slider-demo`, `svg-demo`, `table-demo`, `text-demo`, `textinput-demo`, `theme-demo`, `toast-demo`, `toggle-demo`, `tooltip-demo`, `typography-demo`.

## Options

- `LAMBDA_PLATFORM` - `AUTO` (default), `MACOS`, `LINUX_WAYLAND`, or `LINUX_KMS`.
- `LAMBDA_BUILD_APPS` - default `ON`; set `OFF` to skip real apps.
- `LAMBDA_BUILD_DEMOS` - default `OFF`; set `ON` to build demos.
- `LAMBDA_BUILD_TESTS` - default `OFF`; set `ON` to build unit and reactive tests.
- `LAMBDA_BUILD_BENCHMARKS` - default `OFF`; set `ON` to build micro-benchmarks.
- `LAMBDA_BUILD_WINDOW_MANAGER` - default `ON` on Linux; set `OFF` to skip `lambda-window-manager`.
- `LAMBDA_ENABLE_ASAN` - default `OFF`; set `ON` for AddressSanitizer builds.
- `LAMBDA_ENABLE_DEFAULT_EVENT_LOGGING` - default `OFF`; set `ON` to print default `Application` event handlers.

## Documentation

- [Documentation index](docs/README.md)
- [Project status and roadmap](docs/roadmap.md)
- [Conventions](docs/conventions.md)
- [Reactive graph](docs/reactive-graph.md)
- [Migrating to v5](docs/migrating-to-v5.md)
- [Compositor](docs/compositor.md) (Linux window manager)

Public API headers live under `include/Lambda/`. The umbrella header is [`include/Lambda.hpp`](include/Lambda.hpp). Declarative UI apps usually include [`include/Lambda/UI/UI.hpp`](include/Lambda/UI/UI.hpp) and use `Window::setView`.
