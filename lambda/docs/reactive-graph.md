# Reactive Graph

Lambda v5 uses a fine-grained reactive graph under `Lambda/Reactive`.

## Primitives

- `Signal<T>` stores a value and notifies dependents when `set()` changes it.
- `Computed<T>` lazily derives a value and tracks every signal read during evaluation.
- `Effect` runs a side-effecting closure and re-runs when tracked dependencies change.
- `Scope` owns effects, nested scopes, and cleanup callbacks.
- `Bindable<T>` stores either a constant or a closure that can be evaluated inside an effect.

The `Lambda/Reactive/Reactive.hpp` umbrella also exports convenient aliases in namespace `lambda`: `Signal<T>`, `Computed<T>`, `Effect`, `Scope`, `makeComputed`, `withOwner`, `onCleanup`, and `untrack`.

## Ownership

Every mounted root owns a root `Scope`. Component mounts create child scopes, and control-flow views create branch or row scopes. Destroying a scope disposes its effects and runs registered cleanup callbacks.

```cpp
withOwner(scope, [&] {
  auto count = useState(0);
  useEffect([count] {
    (void)count();
  });
});
```

## UI Bindings

Element modifiers accept constants and `Bindable<T>` values. During mount, Lambda installs effects that evaluate bindables and apply the resulting value to the retained scene node.

```cpp
auto width = useState(120.f);

return Rectangle{}
    .size([width] { return width(); }, 24.f)
    .fill(Color::accent());
```

## Environment

Environment values are explicit compile-time keys. `.environment<Key>(value)` provides a static subtree value, and `.environment<Key>(signal)` provides a signal-backed value. `useEnvironment<Key>()` returns a signal-shaped handle for both static body-time reads and reactive reads inside `Bindable` closures or `Effect` bodies.

`Window` owns a reactive `ThemeKey` signal. Calling `Window::setTheme()` updates retained theme-dependent bindings without remounting the app.
