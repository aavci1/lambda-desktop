# Declarative UI Body Style

Use these conventions for `Element body() const` or `auto body() const` members that return Lambda view trees.

## Indentation

- Use 2 spaces per nesting level.
- Put `return ViewType{` one indent inside `body()`.
- Put designated-initializer fields one indent deeper than the view opener.
- Inside `.children = children(...)`, put each sibling on its own line when the list is not trivial.

## Braces And Types

- Prefer `ViewType{` for designated-initializer trees.
- Use one `.member = value` per line in multi-line initializers.
- Keep short nested aggregates on one line when readable.
- Trailing commas are fine in multi-line initializers.

## Structure

- Use `VStack`, `HStack`, and `ZStack` for layout.
- Keep nested stacks indented at the depth of their sibling.
- Use `Element{...}` only when a composite needs explicit type erasure.
- Use `For`, `Show`, and `Switch` when subtree shape changes reactively.

## Modifiers

Views that support modifiers can chain calls such as:

```cpp
Text{
  .text = "Ready",
  .font = Font::headline(),
  .color = Color::primary(),
}.padding(theme.space3)
 .fill(Color::controlBackground())
 .cornerRadius(theme.radiusMedium);
```

Use `Bindable<T>` overloads when a mounted scene-node property should update after mount.

## Includes

Applications usually include `Lambda.hpp`, `Lambda/UI/UI.hpp`, and the specific view headers they use. Keep examples in the same order as nearby examples: umbrella/core headers, UI headers, view headers, then standard library headers.
