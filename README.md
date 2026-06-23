# lambdaui workspace

This workspace contains **lambdaui**, a C++23 UI framework, plus the Linux-only `lambda-desktop` app suite and the standalone cross-platform `lambda-solitaire` app.

The framework provides a **Metal** canvas on macOS, Linux **Wayland/Vulkan** and **KMS/DRM/Vulkan** backends, vector paths tessellated through [libtess2](https://github.com/memononen/libtess2), platform text layout, and a retained declarative UI layer.

The v5 UI runtime mounts each view tree once, owns reactive state in scopes, and updates retained scene nodes through `Signal`, `Computed`, `Effect`, `Bindable`, animation handles, and reactive environment values.

## Build

Requirements: **CMake 3.25+** and a C++23 compiler.

macOS framework, demos, tests, benchmarks, and `lambda-solitaire` builds need **full Xcode selected as the active developer directory**, because lambdaui compiles and embeds Metal shaders with `xcrun -sdk macosx metal`, `xcrun -sdk macosx metallib`, and `xxd`. Xcode Command Line Tools alone may configure a C++ compiler but still omit the Metal shader compiler. Install Xcode, then run:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
xcodebuild -downloadComponent MetalToolchain
xcrun -sdk macosx -find metal
xcrun -sdk macosx -find metallib
```

Linux Wayland framework builds need `pkg-config`, `wayland-client`, `wayland-cursor`, `wayland-protocols`, `libdrm`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `librsvg-2.0`, `zlib`, and `glslangValidator`. Linux KMS framework builds need `pkg-config`, `libdrm`, `gbm`, `libinput`, `libseat`, `libudev`, `xkbcommon`, `vulkan`, `freetype2`, `fontconfig`, `harfbuzz`, `librsvg-2.0`, `zlib`, and `glslangValidator`.

The Linux-only `lambda-desktop` product additionally needs `libsystemd` for its D-Bus/system-services layer. `tomlplusplus` is fetched only when `LAMBDA_BUILD_DESKTOP=ON`; `libvterm` is fetched only by `lambda-terminal` and needs `perl` to generate its encoding headers.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDAUI_BUILD_TESTS=ON -DLAMBDAUI_BUILD_DEMOS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On macOS, the default build includes `lambdaui` and `lambda-solitaire`; desktop apps are not configured. Framework demos, tests, and benchmarks build as normal terminal executables on macOS rather than `.app` bundles. Solitaire owns its macOS bundle packaging under `solitaire-app/`. On Linux, `LAMBDA_BUILD_DESKTOP` defaults to `ON` and builds the desktop suite under `lambda-desktop/apps/`. Pass `-DLAMBDAUI_BUILD_DEMOS=ON` to build the demo executables under `lambda/demos/`.

## Layout

- `lambda/` owns the `lambdaui` framework/library target, public headers, private sources, tests, benchmarks, and demos.
- `lambda-desktop/` owns Linux desktop apps, services, DBus/system clients, and desktop-wide dependencies such as `tomlplusplus`.
- `lambda-desktop/apps/lambda-terminal/` owns the terminal emulator and its `libvterm` dependency.
- `solitaire-app/` owns the standalone cross-platform `lambda-solitaire` app.

## Apps

Current standalone target:

`lambda-solitaire`.

Linux desktop targets:

`lambda-editor`, `lambda-files`, `lambda-notifications`, `lambda-polkit-agent`, `lambda-portal`, `lambda-preview`, `lambda-secrets`, `lambda-settings`, `lambda-shell`, `lambda-status-notifier-watcher`, `lambda-terminal`, and `lambda-window-manager`.

## Demos

Current demo targets:

`alert-demo`, `animation-demo`, `blend-demo`, `button-demo`, `card-demo`, `checkbox-demo`, `clock-demo`, `cursor-demo`, `gradient-demo`, `hello-world`, `icon-demo`, `image-demo`, `layout-demo`, `markdown-formatter-demo`, `output-demo`, `popover-demo`, `scene-graph-demo`, `scroll-demo`, `segmented-demo`, `select-demo`, `slider-demo`, `svg-demo`, `table-demo`, `text-demo`, `textinput-demo`, `theme-demo`, `toast-demo`, `toggle-demo`, `tooltip-demo`, `typography-demo`.

## Options

- `LAMBDAUI_PLATFORM` - `AUTO` (default), `MACOS`, `LINUX_WAYLAND`, or `LINUX_KMS`.
- `LAMBDA_BUILD_DESKTOP` - default `ON` on Linux and `OFF` elsewhere; builds the Linux-only desktop suite.
- `LAMBDA_BUILD_SOLITAIRE` - default `ON`; builds the standalone Solitaire app.
- `LAMBDAUI_BUILD_DEMOS` - default `OFF`; set `ON` to build demos.
- `LAMBDAUI_BUILD_TESTS` - default `OFF`; set `ON` to build unit and reactive tests.
- `LAMBDAUI_BUILD_BENCHMARKS` - default `OFF`; set `ON` to build micro-benchmarks.
- `LAMBDA_BUILD_TOOLS` - default `ON`; builds developer verification tools.
- `LAMBDA_BUILD_WINDOW_MANAGER` - default `ON` on Linux; set `OFF` to skip `lambda-window-manager`.
- `LAMBDAUI_ENABLE_ASAN` - default `OFF`; set `ON` for AddressSanitizer builds.
- `LAMBDAUI_ENABLE_DEFAULT_EVENT_LOGGING` - default `OFF`; set `ON` to print default `Application` event handlers.

## Documentation

- [Documentation index](docs/README.md)
- [Project status and roadmap](lambda-desktop/docs/roadmap.md)
- [Conventions](docs/conventions.md)
- [Reactive graph](lambda/docs/reactive-graph.md)
- [Migrating to v5](lambda/docs/migrating-to-v5.md)
- [Compositor](lambda-desktop/docs/compositor.md) (Linux window manager)

Public API headers live under `lambda/include/Lambda/`. The umbrella header is [`lambda/include/Lambda.hpp`](lambda/include/Lambda.hpp). Declarative UI apps usually include [`lambda/include/Lambda/UI/UI.hpp`](lambda/include/Lambda/UI/UI.hpp) and use `Window::setView`.
