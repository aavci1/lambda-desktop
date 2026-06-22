# Lambda v5 Codebase Conventions

This document describes the current repository layout and coding patterns.

## Project Identity

- **Name / version:** Lambda v5 (`CMakeLists.txt`: `project(lambda VERSION 5.0.0 ...)`).
- **Platform:** `LAMBDA_PLATFORM` selects one backend at build time: `MACOS`, `LINUX_WAYLAND`, or `LINUX_KMS`. `AUTO` picks macOS on Apple hosts and Wayland on Unix hosts.
- **Language:** C++23, extensions off.
- **Minimum macOS:** 12.0.
- **Framework:** Shared library target `lambda`.
- **Demos:** Optional executable targets in [`lambda/demos/CMakeLists.txt`](../lambda/demos/CMakeLists.txt), enabled with `LAMBDA_BUILD_DEMOS=ON`.
- **Desktop suite:** Linux-only targets under [`lambda-desktop/`](../lambda-desktop), enabled with `LAMBDA_BUILD_DESKTOP=ON`.
- **Standalone app:** `solitaire-app`, enabled with `LAMBDA_BUILD_SOLITAIRE=ON`.

## Build System

- CMake minimum is 3.25.
- C is enabled for vendored `libtess2`; Objective-C and Objective-C++ are enabled only for the macOS backend.
- Framework public includes come from `lambda/include/`; framework private implementation helpers come from `lambda/src/`.
- Desktop-only public includes come from `lambda-desktop/include/`; desktop private implementation helpers come from `lambda-desktop/src/`.
- The `lambda` target builds with `-Wall -Wextra -Wpedantic`.
- Metal shaders compile through `xcrun metal`, `metallib`, and `xxd` into an embedded shader header.
- Dependencies live at their lowest owning product: `libtess2` belongs to the framework, `tomlplusplus` belongs to `lambda-desktop`, and `libvterm` belongs to `lambda-terminal`.
- `lambda_add_app()` in `lambda/cmake/LambdaApp.cmake` creates terminal-style executables, copies runtime fonts next to the binary, and emits Linux `.desktop` metadata. It does not create macOS bundles.
- `solitaire-app/` owns its macOS `.app` bundle, icon conversion, entitlements, and packaging helper locally because it is the only current product that needs that shape.

## Directory Layout

| Path | Role |
|------|------|
| `lambda/` | Cross-platform framework/library, demos, tests, and benchmarks |
| `lambda/include/Lambda/` | Public headers |
| `lambda/include/Lambda.hpp` | Umbrella include |
| `lambda/include/Lambda/Reactive/` | Signals, computed values, effects, scopes, bindings, animation |
| `lambda/include/Lambda/UI/` | Declarative UI, hooks, views, layout, mount runtime |
| `lambda/include/Lambda/SceneGraph/` | Retained scene tree and renderer-facing nodes |
| `lambda/include/Lambda/Graphics/` | Canvas, text, path, and style types |
| `lambda/include/Lambda/Detail/` | Implementation-facing public headers |
| `lambda/src/Core/` | `Application`, `Window`, event loop, platform window factory |
| `lambda/src/UI/` | Mount/runtime/layout implementation |
| `lambda/src/Reactive/` | Non-template reactive and animation implementation |
| `lambda/src/SceneGraph/` | Scene graph storage, traversal, hit testing, rendering |
| `lambda/src/Graphics/` | Portable graphics plus Metal/CoreText implementations |
| `lambda/src/Platform/Mac/` | macOS windowing |
| `lambda/cmake/` | Framework CMake helpers |
| `lambda/resources/` | Framework runtime resources |
| `lambda/protocols/` | Framework-owned Wayland protocol XMLs and stubs |
| `lambda/vendor/` | Header-only vendored framework dependencies |
| `lambda/scripts/` | Framework validation and hygiene scripts |
| `lambda/tools/` | Framework developer utilities |
| `lambda/docs/` | Framework reference documentation |
| `lambda-desktop/` | Linux desktop product entry point, app suite, DBus/system clients, and desktop services |
| `lambda-desktop/include/Lambda/System/` | Desktop-only system-service public headers |
| `lambda-desktop/src/System/` | Desktop-only system-service implementation |
| `lambda-desktop/apps/` | Linux desktop app and service targets |
| `lambda-desktop/docs/` | Linux desktop roadmap, compositor docs, and runbooks |
| `lambda-desktop/scripts/` | Linux desktop verification scripts |
| `lambda-desktop/tools/` | Linux desktop developer verification tools |
| `solitaire-app/` | Standalone cross-platform Solitaire app |
| `lambda/demos/` | Sample apps |
| `lambda/tests/` | Framework tests |
| `lambda/bench/` | Framework benchmarks |
| `docs/` | Repository-level documentation index and conventions |

## Namespace

- Public API lives in `lambda`.
- Reactive primitives live in `lambda::Reactive`.
- `lambda::detail` is reserved for implementation helpers not meant as app-facing API.

## Public And Private Headers

- Headers under `lambda/include/Lambda/...` are framework public API.
- Headers under `lambda-desktop/include/Lambda/...` are desktop-product public API and must not be included by framework code.
- Headers under `lambda/src/...` are private and must not be required by external consumers.
- Public headers must stay Objective-C-free.

## Umbrella Includes

- Use `#include <Lambda.hpp>` for applications that want core, graphics, scene graph, and reactive primitives.
- Use `#include <Lambda/UI/UI.hpp>` for declarative UI applications.
- Finer-grained includes are preferred inside library headers and tests when they reduce dependencies.

## Pimpl

Public owning classes that hide platform or implementation state use:

- nested forward declaration `struct Impl`;
- member `std::unique_ptr<Impl> d`;
- `Impl` definition only in `.cpp` or `.mm`.

This applies to `Application`, `Window`, `EventQueue`, macOS platform implementations, and `CoreTextSystem`.

## Naming

- Types: `PascalCase`.
- Functions and methods: `camelCase`.
- Constants: local constants often use `kName`.
- Private data in implementation structs may use trailing underscores.

## Retained UI

Lambda v5 mounts UI once and updates retained scene nodes through reactive dependencies:

- `MountRoot` owns the root scene node and root `Reactive::Scope`.
- Hooks require an active owner scope.
- `Bindable<T>` modifiers install effects during mount.
- `For`, `Show`, and `Switch` manage dynamic subtree scopes.
- Environment values can be constants or reactive signals.

## Reactive UI

- Hooks that expose state return `Signal<T>`; read with `signal()` and write with
  `signal = value` or `signal.set(value)`.
- `.get()` remains available as an explicit synonym, but examples use `()` for
  read-and-subscribe sites.
- Use `.peek()` for intentional non-tracking reads.
- `useEnvironment<Key>()` returns a signal for the active environment value. Read it
  inside `Bindable` closures or `Effect` bodies when UI should update after
  environment changes. A body-time read is a static mount-time seed and does not
  subscribe.

## Events

- `Event` is a variant of window lifecycle, window, input, timer, and custom events.
- `EventQueue::post`, `dispatch`, and `on` are main-thread-only by contract.
- Custom events use typed payloads wrapped in `CustomEvent`.
- Reactive work and next-frame callbacks are drained by `Application` on the same main loop as events.

## Platform Abstraction

`platform::Window` is private to `lambda/src/UI/Platform`. Portable UI code calls `lambda::platform::createWindow(WindowConfig)`, which is implemented by exactly one platform translation unit in a build.

## Includes

- Headers use `#pragma once`.
- Public project includes use angle brackets and paths relative to `lambda/include/`.
- Private includes from `lambda/src/` use paths relative to the `lambda/src` root.
- `.cpp` files include their corresponding public header first when there is one.

## Examples

Demos are intentionally small and are registered by `lambda_add_demo()` in [`lambda/demos/CMakeLists.txt`](../lambda/demos/CMakeLists.txt). Shared v5 demo scaffolding lives in [`lambda/demos/common/V5ExampleApp.hpp`](../lambda/demos/common/V5ExampleApp.hpp).
