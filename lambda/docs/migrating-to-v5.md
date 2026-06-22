# Migrating To Lambda v5

Lambda v5 is a hard cutover to retained mounting and fine-grained reactivity. Code should move data changes into signals and bind mounted scene-node properties to those signals.

## Component Shape

Keep `body()` as a declarative mount description:

```cpp
struct ToggleSwatch {
  Element body() const {
    auto active = useState(false);

    return Rectangle{}
        .size(48.f, 48.f)
        .fill([active] {
          return active() ? Color::accent() : Color::separator();
        })
        .cornerRadius(8.f)
        .onTap([active] { active = !active(); });
  }
};
```

When text or layout values must keep changing after mount, use a view or modifier with a reactive binding, or use a control-flow view that owns the changing subtree.

## State And Effects

- Use `useState(initial)` for local reactive state. It returns `Signal<T>`.
- Use `useComputed(fn)` for derived values.
- Use `useEffect(fn)` for side effects attached to the current owner scope.
- Use `onCleanup(fn)` when a branch, row, or component owns an external resource.
- Use `untrack(fn)` to read without subscribing.
- Use `signal()` as the canonical read-and-subscribe form. Use `signal.peek()` only for an intentional non-tracking read.

## Control Flow

- Use `For(signal, keyFn, rowFactory)` for keyed lists.
- Use `Show(conditionSignal, thenFactory, elseFactory)` for binary branches.
- Use `Switch(selectorFn, cases)` for multi-branch selection.

Control-flow factories receive their own scopes, so row and branch state persists exactly as long as the mounted subtree exists.

## Theme

Theme is now an explicit environment key. Read the active theme with:

```cpp
auto theme = useEnvironment<ThemeKey>();
```

Read `theme()` inside a `Bindable` closure or `Effect` body when the result must
update after `Window::setTheme(...)` or another signal-backed environment write.
Reads at body time are one-time static reads, which is appropriate for fixed
mount-time layout seeds.

```cpp
Text {
  .text = "Status",
  .color = [theme] { return theme().labelColor; },
}
```

Provide a theme to a subtree with a keyed environment modifier:

```cpp
return content.environment<ThemeKey>(Theme::dark());
```

## Environment Keys

Environment lookups are compile-time keyed. Define a key once, colocating its
value type and default:

```cpp
LAMBDA_DEFINE_ENVIRONMENT_KEY(LocaleKey, Locale, Locale{});
```

Use these APIs:

- `useEnvironment<Key>()` returns a signal-shaped value, using the upstream signal when present or a constant signal otherwise.
- `.environment<Key>(value)` and `.environment<Key>(signal)` provide value-backed and signal-backed subtree overrides.

The old type-keyed API is gone: `useEnvironment<Theme>()` becomes
`useEnvironment<ThemeKey>()`, and `.environment(theme)` becomes
`.environment<ThemeKey>(theme)`.
