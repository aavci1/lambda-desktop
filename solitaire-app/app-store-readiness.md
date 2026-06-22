## Solitaire App — review and App Store readiness

### What you have

A real Klondike Solitaire — not a tech demo. ~3150 lines in one file, but the structure is clean:

- **Game model**: cards/board/history/animations/selection/hint/peek/drag — all aggregate in one immutable `SolitaireState` that flows through a single `Signal<SolitaireState>` with a `mutate(state, fn)` helper. Makes undo trivial (history is just `vector<HistoryEntry>`).
- **Solvable-deal generator** (lines 683-960). Two strategies: direct foundation-order generation (`makeDirectFoundationDeal`) and chain-build (`ChainBuildState`) with relocate/reveal logic. It generates deals **with a witness solution recorded as `vector<WitnessMove>`**. That's not a typical demo — that's product-grade. Casual players hate dead-end Klondike deals; this kills that complaint.
- **Auto-finish** (`autoFinish` line 1640): plays out the win sequence with animations.
- **Hint system** (`showHint` line 1887): finds and bounces a useful card.
- **Fly animations** for deals, drops, wins; **win celebration** with cards bouncing across the field.
- **Settings dialog**: 4 felt themes (Emerald, Sapphire, Obsidian, Crimson).
- **HUD**: New game / hint / undo / auto-finish / settings buttons in a glass pill, plus elapsed time and stats.
- **Drag with peek**: hold to peek face-down cards, drag to move stacks.
- Proper **HiDPI / retina** via `backingScaleFactor` (already in the platform layer).
- **Fullscreen support** already wired in `MacMetalWindow::setFullscreen`.
- **`NSBundle pathForResource`** font loading already works for both bundled and unbundled execution — Material Symbols Rounded font is registered at startup.

This is well above demo quality. As an actual game it could ship.

### What's missing for App Store

In rough order of importance:

**1. Production signing and submission packaging.** `solitaire-app` now owns its `Solitaire.app` bundle path locally, including `Info.plist`, bundled fonts, icon conversion, entitlements, and package helper. App Store submission still needs Apple Distribution signing and the right certificates.

**2. No menu bar.** macOS apps must have at least a basic application menu (App / File / Edit / View / Window / Help). The framework has zero menu support — `NSMenu`/`setMainMenu` aren't referenced anywhere. **Without this, Cmd+Q doesn't quit, Cmd+M doesn't minimize, no About item, no Help menu.** App Store reviewers will reject. Two paths:
- **Quick path**: Create a minimal NSMenu in the solitaire `main.cpp` directly using `<AppKit/AppKit.h>` after `[NSApplication sharedApplication]` runs. Hand-build the menu items. ~80 lines of Obj-C++.
- **Better path**: Add `Application::setMenuBar(MenuBar)` to Lambda. Becomes a generic capability. ~200 lines in framework. Worth doing eventually but not blocking.

For ship, take the quick path.

**3. No production code signing / entitlements / sandbox submission flow.** App Store requires:
- Apple Developer ID + Distribution certificate.
- App Store Connect record (bundle ID, name, screenshots, category, age rating, privacy policy URL).
- Code signed with a Distribution certificate.
- App Sandbox entitlement enabled (`com.apple.security.app-sandbox`).
- Signed and packaged as `.pkg` for App Store submission, or `.dmg` for direct distribution.

For a self-contained Solitaire game with no network, files, or hardware access, the entitlements file is tiny:

```xml
<key>com.apple.security.app-sandbox</key>
<true/>
```

That's it. No file access, no network, no camera. Pure sandbox.

**4. No persistence.** Close the app mid-game → next launch starts a new game, losing state. Users hate this. Solitaire game state is small (few KB serialized). Save to `~/Library/Containers/<bundle-id>/Data/Library/Application Support/...` (sandbox-safe). Load on launch. ~50 lines if you serialize `SolitaireState` to JSON or a simple binary format. Also save the felt theme preference (`NSUserDefaults`).

**5. Icon needs final review.** The target converts `icon.svg` to `.icns` for the bundle. Before submission, verify the generated icon at all required macOS sizes and replace it with final artwork if needed.

**6. No window-state save.** When app re-launches, window goes back to 1200×920 default. macOS users expect window position/size to persist. Two options: enable `NSWindow.restorable = true` (free if you adopt `NSWindowRestoration` — Lambda's MacMetalWindow doesn't currently), or save/restore explicitly in your app code via `NSUserDefaults`. The latter is simpler.

**7. No minimum window size.** Code scales gracefully (`boardGeometry` → `scale = min(availW/kMinBoardW, availH/total)`), but at 400×300 the cards are unreadable. Set `[window setContentMinSize:{800, 600}]` (or similar). Need to add this to `WindowConfig` or do it manually in `main.cpp` via `platformWindow().native()` if exposed.

**8. No accessibility.** Zero VoiceOver support. Cards can't be navigated by keyboard. App Store doesn't strictly require accessibility for games but **Apple really wants it** and it can hold up review for the wrong reviewer. For a card game, the bare minimum is keyboard controls (arrow keys to navigate piles, Enter to select/move). Full VoiceOver would require Lambda to expose AX roles to NSAccessibility — significant framework work. Probably skip for v1; document as a known limitation.

**9. No keyboard shortcuts.** Cmd+Z for undo, Cmd+N for new game, Cmd+H for hint, Cmd+, for settings, Esc to close dialogs. Some of these might already work via the dialog's `dismissOnEscape`. Most don't. **App Store reviewers test keyboard.** Add via the menu bar (where `key equivalent = @"z"` etc. wires up shortcuts). The framework has `useKey` somewhere — let me check whether you'd need it for in-game shortcuts that aren't on the menu.OK, ActionRegistry exists. Probably has shortcut registration. The menu bar can hook into this; in-game shortcuts can be registered via the same path.

**10. No "About Solitaire" / version / copyright info.** Standard menu item. Comes with the menu bar work.

**11. No first-run UX / privacy disclosure.** Apple requires a privacy policy URL even for apps that collect zero data (you have to declare "Data Not Collected" in App Store Connect). No in-app disclosure needed if you don't track anything.

**12. Check for the Apple-tracked things.**
- The app uses `std::random_device` + `nowNanos()` for seeding — fine, no crypto requirement.

**13. Performance.** I don't have numbers for the Solitaire app specifically, but given the framework's text/cache layers and the game's relatively small scene (52 cards + HUD), CPU should be near-zero idle and well under 1ms/frame active. Battery impact: low. App Store has a "Battery Performance" line item — should be fine.

### What's notable in the code

A few things worth flagging that aren't blockers but you should know about:

**`SolitaireState` is copied on every `mutate()`** (line 267-271): `state() → copy → mutate → state = move(copy)`. The state has `Board` + `vector<HistoryEntry>` + `vector<FlyAnimation>` + multiple sub-structs. Each push to history is a `Board` copy. For a 100-move game, history is 100 boards. Each board is ~52 cards × ~20 bytes ≈ 1KB. So history is ~100KB. Per move, a full state copy ≈ 100-500KB. **At 60fps with continuous redraws this would matter; but the mutation happens only on user actions, so it's fine.** Worth noting in case you ever want to add multi-move sequences.

**`useFrame` callback at line 3035** runs every frame and writes to `state` whenever any timer/animation is active. This is the architectural pattern that drives all animations. The `if (changed) state = std::move(current)` guard prevents redraw when nothing actually changed — important for battery.

**Win celebration runs for 35 seconds** (`kWinCelebrationDurationNanos = 35'000'000'000`). That's a lot. Reviewer might accidentally think the app "hung" if they reach this state. Keyboard or click should dismiss.

**No multiplayer, no leaderboards, no networking.** That makes App Store review *much* easier — no privacy questions, no data-collection forms, no Game Center integration questions. Just a single-player game.

### Concrete next steps, ordered

This is the path. Each step independent.

**Phase 1: Finish bundle behavior** (week 1)
1. Keep the existing `solitaire-app` `.app` bundle path green: `Info.plist`, `Contents/MacOS/Solitaire`, bundled fonts, and generated `.icns`.
2. Add minimal `NSMenu` setup in `main.cpp` (App menu with Quit/About/Hide; File menu with New Game; Edit with Undo; Window with Minimize/Zoom; Help). ~80 lines Obj-C++.
3. Set `[window setContentMinSize:{800, 600}]` after window creation.
4. Test bundle builds and runs as `.app`.

**Phase 2: Polish for review** (week 2)
6. App icon: verify the generated `.icns` from `icon.svg`, or replace it with final 1024x1024 source artwork before submission.
7. Persistence: serialize `SolitaireState` to JSON (or sqlite) on every `mutate`, deserialize on launch. Store in `NSApplicationSupportDirectory`.
8. Window-state save: serialize window frame, restore on launch.
9. Keyboard shortcuts: Cmd+N (new game), Cmd+Z (undo), Cmd+H (hint), Cmd+, (settings), Esc (close dialog).
10. About dialog with version, copyright, "Made with Lambda" credit.

**Phase 3: Submission** (week 3)
12. Apple Developer account ($99/year if not already).
13. App Store Connect record: bundle ID, name, screenshots (you need at least one ~1280×800 screenshot per platform), category (public.app-category.card-games), description, keywords, privacy policy URL (a single page on github.io declaring "no data collected" works).
14. Sign with Distribution certificate; set entitlements (`app-sandbox` only); use `productbuild --component App.app /Applications --sign "3rd Party Mac Developer Installer: ..." Output.pkg`.
15. Upload via `xcrun altool` or Transporter.app.
16. Submit for review. **Expected review time: 24-48h these days.**

Common rejection reasons to preempt:
- **No menu bar** — addressed in Phase 1.
- **App doesn't quit on Cmd+Q** — comes with menu bar.
- **App crashes on first launch** — test on a clean macOS install (or VM).
- **Screenshots don't match the actual app** — make sure your screenshots are from the bundle, not from a debug build.
- **Privacy disclosure missing** — declare "Data Not Collected" in App Store Connect.

### What I'd skip for v1

- **VoiceOver / full accessibility** — non-trivial framework work in Lambda. Document as known limitation. Most card games on the App Store don't have it; you can add it post-launch if a reviewer flags it.
- **Game Center / leaderboards** — adds complexity, requires more entitlements, App Store review is fussier.
- **iCloud sync** — adds entitlements, requires CloudKit setup. Skip for v1.
- **iPad / iOS port** — different review queue, different code paths. v1 is macOS only.

### Bottom line

The game itself is well above demo quality and ready to ship gameplay-wise. The real work is **all platform integration**: bundle structure, menu bar, signing, persistence, icon. None of it is hard, but there's no shortcut around it. Realistically: **2-3 weeks of focused work** for someone who hasn't shipped a Mac app before, **3-5 days** for someone who has.

The Lambda framework piece worth investing in: **`Application::setMenuBar` + `Application::registerShortcut`** as first-class APIs. You'll need it for any serious Lambda app on macOS. But for solitaire v1, hand-roll the menu in `main.cpp` and don't block on framework work.
