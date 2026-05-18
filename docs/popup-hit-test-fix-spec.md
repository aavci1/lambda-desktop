# Compositor: popup hit-test fix

**Status:** implemented in `surfaceAt`, with deterministic popup screen-geometry tests. Hardware validation with `foot` still pending.
**Scope:** route pointer input to xdg-popups so they are interactive, not just visible.
**Trigger:** `foot` terminal's right-click context menu appears but cannot be interacted with. Diagnosis confirmed by code inspection (the popup is rendered correctly; the hit test ignores non-toplevel surfaces).

This is a small, focused fix. One commit, ~100 LOC.

---

## 1. The bug

`src/Compositor/Window/WindowManager.cpp` line 86, function `surfaceAt`:

```cpp
WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
    for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
        WaylandServer::Impl::Surface* surface = it->get();
        std::int32_t const width = displayWidth(surface);
        std::int32_t const height = displayHeight(surface);
        if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
        // ... bounds check, return on hit ...
    }
    return nullptr;
}
```

The `!surface->toplevel` guard skips every non-toplevel surface, including popups. Popups have `surface->toplevel == false` (they are not xdg_toplevels; they are xdg_popups). The pointer never resolves to a popup surface, so:

- `wl_pointer.enter` is never sent for the popup.
- Pointer motion within the popup goes to the underlying toplevel.
- Clicks inside the popup register as clicks on the toplevel.
- The toplevel sees a click "outside" the popup and dismisses it via the existing `dismissTopPopupOutside` path.

The popup is visible (rendered in the scene graph) but unreachable.

## 2. The fix

`surfaceAt` consults popups before toplevels. Topmost popup wins. A popup's effective screen position is its parent surface's position plus the popup's configured (positioner-derived) offset.

```cpp
WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
    // Popups first, topmost first.
    for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
        WaylandServer::Impl::XdgPopup* popup = it->get();
        if (!popup || popup->dismissed) continue;
        if (!popup->xdgSurface || !popup->xdgSurface->surface) continue;

        auto const bounds = popupScreenBounds(popup);
        if (!bounds) continue;
        if (x >= bounds->left && x < bounds->right &&
            y >= bounds->top  && y < bounds->bottom) {
            return popup->xdgSurface->surface;
        }
    }
    // Then toplevels (existing logic, unchanged).
    for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
        WaylandServer::Impl::Surface* surface = it->get();
        std::int32_t const width = displayWidth(surface);
        std::int32_t const height = displayHeight(surface);
        if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
        float const left = static_cast<float>(surface->windowX);
        float const top = static_cast<float>(surface->windowY);
        float const right = left + static_cast<float>(width);
        float const bottom = top + static_cast<float>(height);
        if (x >= left && x < right && y >= top && y < bottom) return surface;
    }
    return nullptr;
}
```

The toplevel loop is untouched. The popup loop is new.

## 3. Computing a popup's screen position

Popups don't have `surface->windowX/windowY` set — those are toplevel concepts. The popup struct (in `WaylandServerImpl.hpp`) carries:

```cpp
struct XdgPopup {
    Surface* parentSurface = nullptr;        // parent's Surface (toplevel OR popup)
    std::int32_t configuredX = 0;            // X offset relative to parent
    std::int32_t configuredY = 0;            // Y offset relative to parent
    std::int32_t configuredWidth = 1;
    std::int32_t configuredHeight = 1;
    // ...
};
```

`configuredX/Y` are set during `configurePopup` from the positioner. They are offsets **relative to the parent's content origin**.

Helper to compute absolute screen position:

```cpp
struct PopupBounds {
    float left;
    float top;
    float right;
    float bottom;
};

std::optional<PopupBounds> popupScreenBounds(WaylandServer::Impl::XdgPopup* popup) {
    if (!popup || !popup->parentSurface) return std::nullopt;

    // Walk up the parent chain to find the toplevel anchor.
    // Accumulate offsets from each intermediate popup along the way.
    std::int32_t offsetX = popup->configuredX;
    std::int32_t offsetY = popup->configuredY;
    WaylandServer::Impl::Surface* parent = popup->parentSurface;

    while (parent && !parent->toplevel) {
        // Parent is itself a popup. Find its XdgPopup* to read its parent.
        WaylandServer::Impl::XdgPopup* parentPopup = popupForSurface(parent);
        if (!parentPopup) return std::nullopt;  // dangling chain; bail
        offsetX += parentPopup->configuredX;
        offsetY += parentPopup->configuredY;
        parent = parentPopup->parentSurface;
    }

    if (!parent) return std::nullopt;  // no toplevel anchor found

    float const left = static_cast<float>(parent->windowX + offsetX);
    float const top  = static_cast<float>(parent->windowY + offsetY);
    return PopupBounds{
        left,
        top,
        left + static_cast<float>(popup->configuredWidth),
        top  + static_cast<float>(popup->configuredHeight),
    };
}
```

The `popupForSurface` helper looks up an `XdgPopup*` by its underlying `Surface*` — simple linear search through `server->popups_`. Add it next to `topmostPopup` in `WindowManager.cpp` (small, local helper).

The walk-up loop handles nested popups (submenus). If a popup's parent is another popup, accumulate offsets all the way up to the toplevel. If the chain is broken (a parent popup was destroyed without its children being destroyed first — shouldn't happen but defend), return `nullopt` and the pointer falls through to toplevels as before.

## 4. What else changes

Nothing structural. The fix is contained to `surfaceAt` and a new helper. All existing pointer-routing machinery (`sendPointerFocus`, `wl_pointer.enter/leave/motion/button`, drag, resize, click-to-focus) already operates on a `Surface*` regardless of whether that surface is a toplevel, popup, layer surface, or subsurface. Once `surfaceAt` returns a popup's surface, the rest of the system handles it correctly.

Specifically:

- **`sendPointerFocus(server, popupSurface, timeMs)`** sends `wl_pointer.leave` on the previous surface and `wl_pointer.enter` on the popup, both with the correct serial. foot reads the enter and begins listening for motion/button events on its popup surface.
- **`handlePointerButton`** routes the button to whichever client owns the focused surface. The popup's client is the same as the parent toplevel's client (foot in this case), so the button event goes to foot, which interprets it as a click on the popup menu item.
- **Click-outside dismissal still works.** When the pointer leaves the popup's bounds, `surfaceAt` returns whatever is below (the parent toplevel, or nothing). The existing `dismissTopPopupOutside` path runs on button-press and dismisses the popup. That code path is untouched.

## 5. Subsurfaces under popups

The existing `surfaceAt` doesn't consider subsurfaces either (only toplevels). The same `!surface->toplevel` guard skips them. This is a separate bug — apps that use subsurfaces for floating UI bits won't get input on those subsurfaces — but it's out of scope here. Note in `docs/compositor.md` open questions if not already noted, and address separately.

For popups specifically, hits on the popup's surface route to the popup's surface (which is the popup's wl_surface, the one the client commits buffers to). The client treats input on that surface as input on the popup, which is what it wants.

## 6. Layer surfaces

Layer surfaces (zwlr_layer_shell_v1) also have `!surface->toplevel` and are also currently unreachable by pointer. Same separate-bug status: note it, fix it later. The compositor doesn't yet have a panel or notification daemon that would exercise this. When it does, this same fix pattern applies to layer surfaces (add them to the hit-test ordering, above popups depending on layer).

## 7. Testing

Manual test on hardware: launch the compositor, run `foot`, right-click. The context menu should now respond to hover and click. Selecting a menu item should perform the action (copy, paste, etc.). Selecting nothing and clicking outside should dismiss. Pressing Escape (if foot supports it via keyboard input) should dismiss.

Automated test: add to `tests/CompositorWindowGeometryTests.cpp`:

```cpp
TEST_CASE("Popup hit test resolves to popup surface when pointer is inside its bounds") {
    // Construct a fake server with one toplevel at (100, 100) sized 400x300, and
    // one popup parented to it at offset (50, 200) sized 150x180. The popup's
    // absolute bounds are then (150, 300) to (300, 480).
    // ... build minimal Impl state ...
    auto* hit = surfaceAt(&server, 200.f, 350.f);
    REQUIRE(hit == popupSurface);
}

TEST_CASE("Popup hit test ignores dismissed popups") {
    // Same setup; mark the popup dismissed; pointer in popup's bounds should fall through.
    popup->dismissed = true;
    auto* hit = surfaceAt(&server, 200.f, 350.f);
    REQUIRE(hit == toplevelSurface);
}

TEST_CASE("Nested popup hit test accumulates offsets") {
    // Toplevel at (100, 100). Popup A at offset (50, 50). Popup B parented to A at offset (40, 30).
    // Popup B's absolute origin is (100 + 50 + 40, 100 + 50 + 30) = (190, 180).
    auto* hit = surfaceAt(&server, 195.f, 185.f);
    REQUIRE(hit == popupBSurface);
}
```

Each test constructs minimal `Impl` state — just enough to exercise `surfaceAt` and the new helper. No Wayland server initialization; deterministic.

## 8. Risks

**Low.** The change is additive: the existing toplevel loop is untouched; popups are checked before toplevels. The popup-resolution helper is pure (no side effects). Stale or dangling pointers in `parentSurface` are guarded.

**One real concern:** if `configuredX/configuredY` are wrong (positioner math bug elsewhere), the popup's hit-test region won't match the rendered region. The user would see a popup but be unable to click on it because the hit zone is offset. Mitigation: this is the same data that drives popup rendering, so if rendering looks right, the hit region should match. Visual verification on hardware (foot's right-click menu where the pointer entering the menu changes the cursor and item highlighting starts working) confirms alignment.

## 9. Scope

- Lines changed in `WindowManager.cpp`: ~80 (new helper + extended `surfaceAt`).
- Tests added in `CompositorWindowGeometryTests.cpp`: ~120 (three tests + Impl construction helpers).
- Total: ~200 LOC.

One commit. Half a day at most.

## 10. Acceptance

- ◐ `foot` right-click context menu is interactive: hover highlights items, clicks select items. Needs hardware validation.
- ◐ Clicking outside the menu dismisses it (existing behavior, regression check). Needs hardware validation.
- ◐ Nested submenus (if foot has them or another client does) work the same way. Nested popup bounds are covered by deterministic geometry tests; hardware validation remains.
- ✓ The compositor's existing pointer input fallback keeps non-popup toplevel/layer-surface behavior unchanged.
- ✓ Window geometry tests pass, including popup screen-geometry coverage.

## 11. Follow-ups deferred

- Layer surfaces should be in the hit-test ordering. Same pattern. Separate commit when a real layer-shell client is being tested.
- Subsurfaces should be hit-testable. More involved because subsurface positions are relative to their parent and require coordinate translation; separate work when a client surfaces a need.
- xdg-popup input grab (the spec's force-release-on-timeout safety net) remains deferred. With this fix in place, the next step toward real grab support has cleaner ground to stand on — input routing to popups works without grab, so grab can be added later to *constrain* input rather than to *enable* it.
