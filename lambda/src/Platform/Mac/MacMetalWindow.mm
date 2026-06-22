#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Cursor.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Profile.hpp>

#include "UI/Platform/Window.hpp"
#include "UI/Platform/WindowFactory.hpp"
#include "Graphics/Metal/MetalCanvas.hpp"
#include "UI/DebugFlags.hpp"
#include "UI/TransientPopoverHost.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <dispatch/dispatch.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lambda {
class MacMetalWindow;
class MacPopoverSurface;
class Window;
::lambda::Window* lambdaWindowForPlatform(MacMetalWindow* platform);
CVReturn lambdaHandleDisplayLinkTick(MacMetalWindow* platform);
} // namespace lambda

/// Private AppKit class methods; stable in practice for diagonal window-resize cursors.
@interface NSCursor (LambdaPrivateResizeCursors)
+ (NSCursor*)_windowResizeNorthEastSouthWestCursor;
+ (NSCursor*)_windowResizeNorthWestSouthEastCursor;
@end

@interface LambdaMetalView : NSView <NSTextInputClient>
@property(nonatomic, assign) lambda::MacMetalWindow* lambdaPlatform;
- (CAMetalLayer*)lambdaMetalLayer;
- (void)updateDrawableSize;
- (BOOL)lambdaWantsTextInput;
- (void)lambdaHandleDisplayLink:(id)displayLink;
@end

@interface LambdaPopupMenuTarget : NSObject {
@public
  lambda::Window* lambdaWindow;
  std::vector<std::function<void()>> handlers;
  std::vector<std::string> actionNames;
}
- (void)lambdaPopupMenuAction:(id)sender;
@end

@interface LambdaPopoverView : NSView <NSTextInputClient>
@property(nonatomic, assign) lambda::MacPopoverSurface* surface;
- (CAMetalLayer*)lambdaMetalLayer;
- (void)updateDrawableSize;
@end

@interface LambdaPopoverDelegate : NSObject <NSPopoverDelegate>
@property(nonatomic, assign) lambda::MacMetalWindow* platform;
@property(nonatomic, assign) std::uint64_t popoverId;
@end

namespace lambda {
namespace detail {
void postInputFromView(LambdaMetalView* view, InputEvent::Kind kind, NSEvent* e, std::string text = {});
void postTextInput(LambdaMetalView* view, std::string text);
} // namespace detail
} // namespace lambda

@implementation LambdaMetalView

/// NSView may use `NSViewBackingLayer` unless we supply a Metal layer here.
/// `+layerClass` alone is not always reliable on newer macOS for `setDevice:`.
- (CALayer*)makeBackingLayer {
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device) {
    metalLayer.device = device;
  }
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = NO;
  metalLayer.contentsScale = [NSScreen mainScreen].backingScaleFactor;
  metalLayer.contentsGravity = kCAGravityTopLeft;
  metalLayer.opaque = NO;
  metalLayer.backgroundColor = [[NSColor clearColor] CGColor];
  // Match MetalCanvas in-flight limit; helps avoid main-thread stalls on nextDrawable during live resize.
  metalLayer.maximumDrawableCount = 3;
  metalLayer.allowsNextDrawableTimeout = YES;
  if (@available(macOS 10.13, *)) {
    // Keep CAMetalLayer presentation display-synced; Lambda's frame scheduler controls when frames are encoded.
    metalLayer.displaySyncEnabled = YES;
  }

  // `presentsWithTransaction` is toggled only around resize-driven flush (see windowDidResize). Leaving it
  // always YES can defer the first composite until a later CA transaction and cause an intermittent blank window.
  metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  metalLayer.needsDisplayOnBoundsChange = YES;

  return metalLayer;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    self.wantsLayer = YES;
    self.layer.opaque = NO;
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
    self.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    [self updateDrawableSize];
  }
  return self;
}

// Lambda owns cursor state. Prevent NSView's cursor-rect machinery from
// registering any rects on this view, so AppKit won't set cursors behind us.
- (void)resetCursorRects {
  // Intentionally empty.
}

- (CAMetalLayer*)lambdaMetalLayer {
  CALayer* layer = self.layer;
  if ([layer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(layer);
  }
  return nil;
}

- (CGFloat)lambdaBackingScale {
  NSWindow* w = self.window;
  if (w) {
    return w.backingScaleFactor;
  }
  return [NSScreen mainScreen].backingScaleFactor;
}

- (void)layout {
  [super layout];
  [self updateDrawableSize];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (metalLayer && self.window) {
    metalLayer.contentsScale = self.window.backingScaleFactor;
  }
  [self updateDrawableSize];
  [self updateTrackingAreas];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (metalLayer && self.window) {
    metalLayer.contentsScale = self.window.backingScaleFactor;
  }
  [self updateDrawableSize];
}

- (void)updateDrawableSize {
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (!metalLayer) {
    return;
  }
  CGFloat scale = [self lambdaBackingScale];
  CGSize bounds = self.bounds.size;
  CGFloat w = (std::max)(bounds.width * scale, static_cast<CGFloat>(1.0));
  CGFloat h = (std::max)(bounds.height * scale, static_cast<CGFloat>(1.0));
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  metalLayer.contentsGravity = kCAGravityTopLeft;
  metalLayer.drawableSize = CGSizeMake(w, h);
  [CATransaction commit];
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)isOpaque {
  return NO;
}

- (BOOL)isFlipped {
  return YES;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  for (NSTrackingArea* area in self.trackingAreas) {
    [self removeTrackingArea:area];
  }
  NSTrackingAreaOptions opts =
      NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInKeyWindow |
      NSTrackingInVisibleRect | NSTrackingEnabledDuringMouseDrag;
  NSTrackingArea* ta =
      [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
  [self addTrackingArea:ta];
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self updateTrackingAreas];
}

- (void)keyDown:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::KeyDown, event);
  if ([self lambdaWantsTextInput]) {
    [self interpretKeyEvents:@[event]];
  }
}

- (void)keyUp:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::KeyUp, event);
}

- (void)doCommandBySelector:(SEL)selector {
  (void)selector;
}

- (BOOL)hasMarkedText {
  return NO;
}

- (NSRange)markedRange {
  return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
  return NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
  (void)string;
  (void)selectedRange;
  (void)replacementRange;
}

- (void)unmarkText {
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
  return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                                actualRange:(NSRangePointer)actualRange {
  (void)range;
  (void)actualRange;
  return nil;
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
  (void)replacementRange;
  NSString* s = nil;
  if ([string isKindOfClass:[NSAttributedString class]]) {
    s = [(NSAttributedString*)string string];
  } else if ([string isKindOfClass:[NSString class]]) {
    s = (NSString*)string;
  }
  std::string utf8 = s ? [s UTF8String] : "";
  lambda::detail::postTextInput(self, std::move(utf8));
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
  (void)point;
  return NSNotFound;
}

- (BOOL)lambdaWantsTextInput {
  lambda::MacMetalWindow* platform = self.lambdaPlatform;
  lambda::Window* window = lambda::lambdaWindowForPlatform(platform);
  return window && window->wantsTextInput();
}

- (NSTextInputContext*)inputContext {
  if (![self lambdaWantsTextInput]) {
    return nil;
  }
  return [super inputContext];
}

- (void)lambdaHandleDisplayLink:(id)displayLink {
  (void)displayLink;
  lambda::MacMetalWindow* platform = self.lambdaPlatform;
  if (!platform) {
    return;
  }
  (void)lambda::lambdaHandleDisplayLinkTick(platform);
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
  (void)range;
  (void)actualRange;
  NSWindow* w = self.window;
  return w ? [w convertRectToScreen:w.frame] : NSZeroRect;
}

@end

@implementation LambdaPopupMenuTarget

- (void)lambdaPopupMenuAction:(id)sender {
  NSMenuItem* item = [sender isKindOfClass:[NSMenuItem class]] ? sender : nil;
  if (!item) {
    return;
  }
  NSInteger const tag = item.tag;
  if (tag < 0 || static_cast<std::size_t>(tag) >= handlers.size()) {
    return;
  }
  std::function<void()> const& handler = handlers[static_cast<std::size_t>(tag)];
  if (handler) {
    handler();
    return;
  }
  if (static_cast<std::size_t>(tag) < actionNames.size() && lambdaWindow && !actionNames[static_cast<std::size_t>(tag)].empty()) {
    lambdaWindow->dispatchAction(actionNames[static_cast<std::size_t>(tag)]);
  }
}

@end

@interface LambdaWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) lambda::MacMetalWindow* platform;
@end

namespace lambda {

namespace {

std::atomic<unsigned int> gNextHandle{1};

NSString* ns(std::string const& text) {
  NSString* out = [NSString stringWithUTF8String:text.c_str()];
  return out ? out : @"";
}

bool popupItemEnabled(lambda::MenuItem const& item, lambda::Window* window) {
  if (item.isEnabled && !item.isEnabled()) {
    return false;
  }
  if (!item.actionName.empty() && window) {
    return window->isActionEnabled(item.actionName);
  }
  return true;
}

void addPopupMenuItem(NSMenu* menu, lambda::MenuItem const& item, LambdaPopupMenuTarget* target) {
  if (item.role == lambda::MenuRole::Separator) {
    [menu addItem:[NSMenuItem separatorItem]];
    return;
  }

  if (item.role == lambda::MenuRole::Submenu) {
    NSMenuItem* submenuItem = [[NSMenuItem alloc] initWithTitle:ns(item.label)
                                                        action:nil
                                                 keyEquivalent:@""];
    NSMenu* submenu = [[NSMenu alloc] initWithTitle:ns(item.label)];
    for (lambda::MenuItem const& child : item.children) {
      addPopupMenuItem(submenu, child, target);
    }
    submenuItem.submenu = submenu;
    submenuItem.enabled = popupItemEnabled(item, target ? target->lambdaWindow : nullptr);
    [menu addItem:submenuItem];
    return;
  }

  NSInteger const tag = static_cast<NSInteger>(target ? target->handlers.size() : 0u);
  if (target) {
    target->handlers.push_back(item.handler);
    target->actionNames.push_back(item.actionName);
  }
  NSMenuItem* nsItem = [[NSMenuItem alloc] initWithTitle:ns(item.label)
                                                  action:@selector(lambdaPopupMenuAction:)
                                           keyEquivalent:@""];
  nsItem.target = target;
  nsItem.tag = tag;
  nsItem.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
  nsItem.enabled = popupItemEnabled(item, target ? target->lambdaWindow : nullptr) &&
                   (static_cast<bool>(item.handler) || !item.actionName.empty());
  [menu addItem:nsItem];
}

std::int64_t nowSteadyClockNanos() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

class MacMetalWindow : public platform::Window {
public:
  explicit MacMetalWindow(const WindowConfig& config);
  ~MacMetalWindow() override;

  void setLambdaWindow(::lambda::Window* window) override;
  void show() override;
  void resize(const Size& newSize) override;
  void setMinSize(Size size) override;
  void setMaxSize(Size size) override;
  void setFullscreen(bool fullscreen) override;
  void setTitle(const std::string& title) override;
  void setTitlebarMode(WindowTitlebarMode mode) override;
  WindowTitlebarMode titlebarMode() const override;
  void setBackground(WindowBackground const& background) override;
  WindowChromeMetrics chromeMetrics() const override;
  void beginWindowDrag(std::uint32_t platformSerial = 0) override;
  void beginWindowResize(WindowResizeEdge edge, std::uint32_t platformSerial = 0) override;
  bool showPopupMenu(PopupMenu menu, Rect anchor, std::uint32_t platformSerial = 0) override;
  PopoverSurfaceId showPopover(Popover popover, Rect anchor, std::uint32_t platformSerial = 0) override;
  void repositionPopover(PopoverSurfaceId id, Popover const& popover, Rect anchor) override;
  void dismissPopover(PopoverSurfaceId id) override;
  Size currentSize() const override;
  std::optional<Rect> currentFrame() const override;
  void setFrame(Rect frame) override;
  bool isFullscreen() const override;
  unsigned int handle() const override;
  void* nativeGraphicsSurface() const override;

  std::unique_ptr<Canvas> createCanvas(::lambda::Window& owner) override;

  void processEvents() override;
  void waitForEvents(int timeoutMs) override;
  void wakeEventLoop() override;
  void requestAnimationFrame() override;
  void acknowledgeAnimationFrameTick() override;
  void completeAnimationFrame(bool needsAnotherFrame) override;

  void setCursor(Cursor kind) override;
  [[nodiscard]] PlatformWindowCapabilities capabilities() const override;
  void rememberPointerDownEvent(NSEvent* event);

  ::lambda::Window* lambdaWindow() const;
  CVReturn onDisplayLinkTick();
  void handlePopoverClosed(PopoverSurfaceId id);

  /// Enables CAMetalLayer transaction presentation only for resize flushes (paired with MetalCanvas sync present).
  void setMetalLayerPresentsWithTransaction(bool enable);
  void positionNativeWindowControls();

private:
  void setModernDisplayLinkPaused(bool paused);
  void applyTitlebarMode();
  void applyBackground();

  struct Impl;
  std::unique_ptr<Impl> d;
};

class MacPopoverSurface {
public:
  MacPopoverSurface(MacMetalWindow* owner, PopoverSurfaceId id, Popover popover);
  ~MacPopoverSurface();

  MacPopoverSurface(MacPopoverSurface const&) = delete;
  MacPopoverSurface& operator=(MacPopoverSurface const&) = delete;

  PopoverSurfaceId id() const noexcept { return id_; }
  bool show(LambdaMetalView* parentView, Rect anchor);
  void reposition(Popover const& popover, Rect anchor);
  void close();
  void notifyNativeClosed();

  bool dispatchingEvent() const noexcept { return dispatchDepth_ > 0; }
  void requestCloseAfterEvent() noexcept { closeAfterEvent_ = true; }

  void render();
  void handlePointerDown(NSEvent* event);
  void handlePointerUp(NSEvent* event);
  void handlePointerMove(NSEvent* event);
  void handleScroll(NSEvent* event);
  void handleKeyDown(NSEvent* event);
  void handleKeyUp(NSEvent* event);
  void handleTextInput(std::string text);

private:
  struct EventScope {
    explicit EventScope(MacPopoverSurface& surface) : surface(surface) { ++surface.dispatchDepth_; }
    ~EventScope() {
      if (--surface.dispatchDepth_ == 0 && surface.closeAfterEvent_ && surface.owner_) {
        MacMetalWindow* owner = surface.owner_;
        PopoverSurfaceId const id = surface.id_;
        owner->dismissPopover(id);
      }
    }
    MacPopoverSurface& surface;
  };

  Point pointForEvent(NSEvent* event) const;
  NSRectEdge preferredEdge() const;

  MacMetalWindow* owner_ = nullptr;
  PopoverSurfaceId id_{};
  Popover popover_{};
  LambdaMetalView* parentView_ = nil;
  NSPopover* nativePopover_ = nil;
  NSViewController* controller_ = nil;
  LambdaPopoverView* view_ = nil;
  LambdaPopoverDelegate* delegate_ = nil;
  std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<TransientPopoverHost> host_;
  Size size_{};
  int dispatchDepth_ = 0;
  bool closeAfterEvent_ = false;
  bool closing_ = false;
};

CVReturn displayLinkOutputCallback(CVDisplayLinkRef /*displayLink*/, CVTimeStamp const* /*now*/,
                                   CVTimeStamp const* /*outputTime*/, CVOptionFlags /*flagsIn*/,
                                   CVOptionFlags* /*flagsOut*/, void* userInfo) {
  auto* platform = static_cast<MacMetalWindow*>(userInfo);
  if (!platform) {
    return kCVReturnSuccess;
  }
  return platform->onDisplayLinkTick();
}

struct MacMetalWindow::Impl {
  NSWindow* window_{nil};
  NSVisualEffectView* materialView_{nil};
  NSView* tintView_{nil};
  LambdaMetalView* metalView_{nil};
  LambdaWindowDelegate* delegate_{nil};
  ::lambda::Window* lambdaWindow_{nullptr};
  unsigned int handle_{0};
  id displayLink_ = nil;
  CVDisplayLinkRef legacyDisplayLink_{nullptr};
  std::atomic<bool> frameRequested_{false};
  std::atomic<bool> frameEventQueued_{false};
  std::atomic<bool> legacyDisplayLinkRunning_{false};
  Cursor currentCursor_{Cursor::Inherit};
  WindowTitlebarMode titlebarMode_{WindowTitlebarMode::System};
  WindowBackground background_{};
  NSEvent* lastPointerDownEvent_{nil};
  std::vector<std::unique_ptr<MacPopoverSurface>> popovers_;
  std::uint64_t nextPopoverId_{1};
};

namespace detail {

Modifiers modifiersFromFlags(NSUInteger m) {
  Modifiers r = Modifiers::None;
  if (m & NSEventModifierFlagShift) {
    r = r | Modifiers::Shift;
  }
  if (m & NSEventModifierFlagControl) {
    r = r | Modifiers::Ctrl;
  }
  if (m & NSEventModifierFlagOption) {
    r = r | Modifiers::Alt;
  }
  if (m & NSEventModifierFlagCommand) {
    r = r | Modifiers::Meta;
  }
  return r;
}

Modifiers modifiersFromNSEvent(NSEvent* e) { return modifiersFromFlags(e.modifierFlags); }

MouseButton buttonFromNSEvent(NSEvent* e) {
  switch (e.buttonNumber) {
  case 0:
    return MouseButton::Left;
  case 1:
    return MouseButton::Right;
  case 2:
    return MouseButton::Middle;
  default:
    return MouseButton::Other;
  }
}

bool lambdaDebugInputMacPost() {
  return debug::inputEnabled();
}

void postInputFromView(LambdaMetalView* view, InputEvent::Kind kind, NSEvent* e, std::string text) {
  MacMetalWindow* p = view.lambdaPlatform;
  if (!p || !p->lambdaWindow()) {
    if (lambdaDebugInputMacPost()) {
      std::fprintf(stderr, "[lambda:input:mac] postInputFromView: no platform/window (dropped)\n");
    }
    return;
  }
  if (kind == InputEvent::Kind::PointerDown) {
    p->rememberPointerDownEvent(e);
  }
  InputEvent ie;
  ie.kind = kind;
  ie.handle = p->lambdaWindow()->handle();
  if (kind == InputEvent::Kind::Scroll) {
    NSPoint pt = [view convertPoint:[e locationInWindow] fromView:nil];
    ie.position = Vec2{static_cast<float>(pt.x), static_cast<float>(pt.y)};
    ie.scrollDelta =
        Vec2{static_cast<float>(e.scrollingDeltaX), static_cast<float>(e.scrollingDeltaY)};
    ie.preciseScrollDelta = static_cast<bool>(e.hasPreciseScrollingDeltas);
  } else {
    NSPoint pt = [view convertPoint:[e locationInWindow] fromView:nil];
    ie.position = Vec2{static_cast<float>(pt.x), static_cast<float>(pt.y)};
  }
  ie.button = (kind == InputEvent::Kind::PointerEnter ||
               kind == InputEvent::Kind::PointerLeave ||
               kind == InputEvent::Kind::PointerMove ||
               kind == InputEvent::Kind::Scroll)
                  ? MouseButton::None
                  : buttonFromNSEvent(e);
  ie.key = 0;
  if (kind == InputEvent::Kind::KeyDown || kind == InputEvent::Kind::KeyUp) {
    ie.key = static_cast<KeyCode>(e.keyCode);
  }
  ie.modifiers = modifiersFromNSEvent(e);
  {
    std::uint8_t pb = static_cast<std::uint8_t>([NSEvent pressedMouseButtons] & 0xFF);
    // Session-state can reflect a physical release before AppKit's bitmask updates when mouseUp
    // was not delivered to this window (e.g. release outside the window during a drag).
    if (!CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft)) {
      pb &= static_cast<std::uint8_t>(~1u);
    }
    ie.pressedButtons = pb;
  }
  ie.text = std::move(text);
  if (lambdaDebugInputMacPost()) {
    if (kind == InputEvent::Kind::Scroll) {
      std::fprintf(stderr,
                   "[lambda:input:mac] post Scroll handle=%u pos=(%.1f,%.1f) delta=(%.2f,%.2f)\n",
                   static_cast<unsigned>(ie.handle), static_cast<double>(ie.position.x),
                   static_cast<double>(ie.position.y), static_cast<double>(ie.scrollDelta.x),
                   static_cast<double>(ie.scrollDelta.y));
    } else if (kind == InputEvent::Kind::PointerMove) {
      static int moveN;
      if (++moveN % 20 == 1) {
        std::fprintf(stderr, "[lambda:input:mac] post PointerMove handle=%u pos=(%.1f,%.1f) (sampled)\n",
                     static_cast<unsigned>(ie.handle), static_cast<double>(ie.position.x),
                     static_cast<double>(ie.position.y));
      }
    } else {
      char const* kn = "?";
      switch (kind) {
      case InputEvent::Kind::PointerEnter:
        kn = "PointerEnter";
        break;
      case InputEvent::Kind::PointerLeave:
        kn = "PointerLeave";
        break;
      case InputEvent::Kind::PointerDown:
        kn = "PointerDown";
        break;
      case InputEvent::Kind::PointerUp:
        kn = "PointerUp";
        break;
      default:
        break;
      }
      std::fprintf(stderr, "[lambda:input:mac] post %s handle=%u pos=(%.1f,%.1f)\n", kn,
                   static_cast<unsigned>(ie.handle), static_cast<double>(ie.position.x),
                   static_cast<double>(ie.position.y));
    }
  }
  Application::instance().eventQueue().post(ie);
}

void postTextInput(LambdaMetalView* view, std::string text) {
  MacMetalWindow* p = view.lambdaPlatform;
  if (!p || !p->lambdaWindow()) {
    return;
  }
  InputEvent ie;
  ie.kind = InputEvent::Kind::TextInput;
  ie.handle = p->lambdaWindow()->handle();
  ie.modifiers = modifiersFromFlags([NSEvent modifierFlags]);
  ie.text = std::move(text);
  Application::instance().eventQueue().post(ie);
}

} // namespace detail

namespace {

NSRect nsRect(Rect rect) {
  return NSMakeRect(static_cast<CGFloat>(rect.x),
                    static_cast<CGFloat>(rect.y),
                    static_cast<CGFloat>(std::max(1.f, rect.width)),
                    static_cast<CGFloat>(std::max(1.f, rect.height)));
}

Size popoverMaxSize(MacMetalWindow* owner, Popover const& popover) {
  if (popover.maxSize) {
    return Size{std::max(1.f, popover.maxSize->width), std::max(1.f, popover.maxSize->height)};
  }
  Size const parent = owner ? owner->currentSize() : Size{480.f, 360.f};
  return Size{std::max(1.f, parent.width - 24.f), std::max(1.f, parent.height - 24.f)};
}

} // namespace

MacPopoverSurface::MacPopoverSurface(MacMetalWindow* owner, PopoverSurfaceId id, Popover popover)
    : owner_(owner), id_(id), popover_(std::move(popover)) {
  ::lambda::Window* window = owner_ ? owner_->lambdaWindow() : nullptr;
  EnvironmentBinding environment = window ? window->environmentBinding() : EnvironmentBinding{};
  Size const maxSize = popoverMaxSize(owner_, popover_);
  host_ = std::make_unique<TransientPopoverHost>(TransientPopoverHost::Config{
      .popover = popover_,
      .environment = std::move(environment),
      .maxSize = maxSize,
      .useNativeShell = true,
      .requestRedraw = [this] {
        render();
      },
      .requestDismiss = [this] {
        if (owner_) {
          owner_->dismissPopover(id_);
        }
      },
  });
  size_ = host_->measuredSize();
}

MacPopoverSurface::~MacPopoverSurface() {
  close();
  if (view_) {
    view_.surface = nullptr;
  }
  if (delegate_) {
    delegate_.platform = nullptr;
  }
  canvas_.reset();
  host_.reset();
}

bool MacPopoverSurface::show(LambdaMetalView* parentView, Rect anchor) {
  if (!owner_ || !owner_->lambdaWindow() || !parentView || !host_) {
    return false;
  }
  parentView_ = parentView;

  nativePopover_ = [[NSPopover alloc] init];
  nativePopover_.behavior = popover_.dismissOnOutsideTap ? NSPopoverBehaviorTransient
                                                         : NSPopoverBehaviorApplicationDefined;
  nativePopover_.animates = YES;

  delegate_ = [[LambdaPopoverDelegate alloc] init];
  delegate_.platform = owner_;
  delegate_.popoverId = id_.value;
  nativePopover_.delegate = delegate_;

  controller_ = [[NSViewController alloc] init];
  controller_.preferredContentSize = NSMakeSize(static_cast<CGFloat>(size_.width),
                                                static_cast<CGFloat>(size_.height));
  view_ = [[LambdaPopoverView alloc] initWithFrame:NSMakeRect(0, 0,
                                                            static_cast<CGFloat>(size_.width),
                                                            static_cast<CGFloat>(size_.height))];
  view_.surface = this;
  controller_.view = view_;
  nativePopover_.contentViewController = controller_;

  CAMetalLayer* layer = [view_ lambdaMetalLayer];
  if (!layer) {
    return false;
  }
  canvas_ = createMetalCanvas(owner_->lambdaWindow(), (__bridge void*)layer, owner_->handle(),
                              Application::instance().textSystem(), [this] {
                                render();
                              });
  if (!canvas_) {
    return false;
  }
  canvas_->updateDpiScale(static_cast<float>([parentView window].backingScaleFactor),
                          static_cast<float>([parentView window].backingScaleFactor));
  canvas_->resize(static_cast<int>(std::lround(size_.width)), static_cast<int>(std::lround(size_.height)));
  host_->mount(size_);

  [nativePopover_ showRelativeToRect:nsRect(anchor) ofView:parentView preferredEdge:preferredEdge()];
  if (view_.window) {
    [view_.window makeFirstResponder:view_];
  }
  render();
  return true;
}

void MacPopoverSurface::reposition(Popover const& popover, Rect anchor) {
  if (closing_ || !nativePopover_ || !parentView_) {
    return;
  }
  popover_.resolvedPlacement = popover.resolvedPlacement;
  [nativePopover_ showRelativeToRect:nsRect(anchor) ofView:parentView_ preferredEdge:preferredEdge()];
}

void MacPopoverSurface::close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  if (nativePopover_) {
    nativePopover_.delegate = nil;
    [nativePopover_ close];
  }
  if (host_) {
    host_->notifyDismissed();
  }
}

void MacPopoverSurface::notifyNativeClosed() {
  if (closing_) {
    return;
  }
  closing_ = true;
  if (nativePopover_) {
    nativePopover_.delegate = nil;
  }
  if (host_) {
    host_->notifyDismissed();
  }
}

void MacPopoverSurface::render() {
  if (closing_ || !view_ || !canvas_ || !host_) {
    return;
  }
  [view_ updateDrawableSize];
  canvas_->resize(static_cast<int>(std::lround(std::max(1.f, size_.width))),
                  static_cast<int>(std::lround(std::max(1.f, size_.height))));
  canvas_->beginFrame();
  host_->render(*canvas_);
  canvas_->present();
}

Point MacPopoverSurface::pointForEvent(NSEvent* event) const {
  if (!view_ || !event) {
    return {};
  }
  NSPoint const point = [view_ convertPoint:[event locationInWindow] fromView:nil];
  return Point{static_cast<float>(point.x), static_cast<float>(point.y)};
}

void MacPopoverSurface::handlePointerDown(NSEvent* event) {
  if (!host_) {
    return;
  }
  EventScope scope(*this);
  host_->pointerDown(pointForEvent(event), detail::buttonFromNSEvent(event));
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handlePointerUp(NSEvent* event) {
  if (!host_) {
    return;
  }
  EventScope scope(*this);
  host_->pointerUp(pointForEvent(event), detail::buttonFromNSEvent(event));
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handlePointerMove(NSEvent* event) {
  if (!host_) {
    return;
  }
  EventScope scope(*this);
  host_->pointerMove(pointForEvent(event));
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handleScroll(NSEvent* event) {
  if (!host_ || !event) {
    return;
  }
  EventScope scope(*this);
  host_->scroll(pointForEvent(event), Vec2{static_cast<float>(event.scrollingDeltaX),
                                           static_cast<float>(event.scrollingDeltaY)});
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handleKeyDown(NSEvent* event) {
  if (!host_ || !event) {
    return;
  }
  EventScope scope(*this);
  host_->keyDown(static_cast<KeyCode>(event.keyCode), detail::modifiersFromNSEvent(event));
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handleKeyUp(NSEvent* event) {
  if (!host_ || !event) {
    return;
  }
  EventScope scope(*this);
  host_->keyUp(static_cast<KeyCode>(event.keyCode), detail::modifiersFromNSEvent(event));
  if (!closeAfterEvent_) {
    render();
  }
}

void MacPopoverSurface::handleTextInput(std::string text) {
  if (!host_ || text.empty()) {
    return;
  }
  EventScope scope(*this);
  host_->textInput(text);
  if (!closeAfterEvent_) {
    render();
  }
}

NSRectEdge MacPopoverSurface::preferredEdge() const {
  switch (popover_.resolvedPlacement) {
  case PopoverPlacement::Above:
    return NSMinYEdge;
  case PopoverPlacement::Below:
    return NSMaxYEdge;
  case PopoverPlacement::Start:
    return NSMinXEdge;
  case PopoverPlacement::End:
    return NSMaxXEdge;
  }
  return NSMaxYEdge;
}

} // namespace lambda

@implementation LambdaPopoverView

- (CALayer*)makeBackingLayer {
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device) {
    metalLayer.device = device;
  }
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = NO;
  metalLayer.opaque = NO;
  metalLayer.backgroundColor = [[NSColor clearColor] CGColor];
  metalLayer.contentsScale = [NSScreen mainScreen].backingScaleFactor;
  metalLayer.maximumDrawableCount = 3;
  metalLayer.allowsNextDrawableTimeout = YES;
  if (@available(macOS 10.13, *)) {
    metalLayer.displaySyncEnabled = YES;
  }
  metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  metalLayer.needsDisplayOnBoundsChange = YES;
  return metalLayer;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    self.wantsLayer = YES;
    self.layer.opaque = NO;
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    [self updateDrawableSize];
  }
  return self;
}

- (CAMetalLayer*)lambdaMetalLayer {
  CALayer* layer = self.layer;
  if ([layer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(layer);
  }
  return nil;
}

- (CGFloat)lambdaBackingScale {
  NSWindow* w = self.window;
  return w ? w.backingScaleFactor : [NSScreen mainScreen].backingScaleFactor;
}

- (void)layout {
  [super layout];
  [self updateDrawableSize];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (metalLayer && self.window) {
    metalLayer.contentsScale = self.window.backingScaleFactor;
  }
  [self updateDrawableSize];
  [self updateTrackingAreas];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (metalLayer && self.window) {
    metalLayer.contentsScale = self.window.backingScaleFactor;
  }
  [self updateDrawableSize];
}

- (void)updateDrawableSize {
  CAMetalLayer* metalLayer = [self lambdaMetalLayer];
  if (!metalLayer) {
    return;
  }
  CGFloat const scale = [self lambdaBackingScale];
  CGSize const bounds = self.bounds.size;
  metalLayer.drawableSize = CGSizeMake((std::max)(bounds.width * scale, static_cast<CGFloat>(1.0)),
                                       (std::max)(bounds.height * scale, static_cast<CGFloat>(1.0)));
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)isOpaque {
  return NO;
}

- (BOOL)isFlipped {
  return YES;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  for (NSTrackingArea* area in self.trackingAreas) {
    [self removeTrackingArea:area];
  }
  NSTrackingAreaOptions opts =
      NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways |
      NSTrackingInVisibleRect | NSTrackingEnabledDuringMouseDrag;
  NSTrackingArea* ta = [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
  [self addTrackingArea:ta];
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self updateTrackingAreas];
}

- (void)mouseDown:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerDown(event);
}

- (void)mouseUp:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerUp(event);
}

- (void)mouseMoved:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerMove(event);
}

- (void)mouseDragged:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerMove(event);
}

- (void)rightMouseDown:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerDown(event);
}

- (void)rightMouseUp:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerUp(event);
}

- (void)otherMouseDown:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerDown(event);
}

- (void)otherMouseUp:(NSEvent*)event {
  if (self.surface) self.surface->handlePointerUp(event);
}

- (void)scrollWheel:(NSEvent*)event {
  if (self.surface) self.surface->handleScroll(event);
}

- (void)keyDown:(NSEvent*)event {
  if (self.surface) self.surface->handleKeyDown(event);
  [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent*)event {
  if (self.surface) self.surface->handleKeyUp(event);
}

- (void)doCommandBySelector:(SEL)selector {
  (void)selector;
}

- (BOOL)hasMarkedText {
  return NO;
}

- (NSRange)markedRange {
  return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
  return NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
  (void)string;
  (void)selectedRange;
  (void)replacementRange;
}

- (void)unmarkText {
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
  return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                                actualRange:(NSRangePointer)actualRange {
  (void)range;
  (void)actualRange;
  return nil;
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
  (void)replacementRange;
  NSString* s = nil;
  if ([string isKindOfClass:[NSAttributedString class]]) {
    s = [(NSAttributedString*)string string];
  } else if ([string isKindOfClass:[NSString class]]) {
    s = (NSString*)string;
  }
  if (self.surface) {
    self.surface->handleTextInput(s ? std::string([s UTF8String]) : std::string{});
  }
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
  (void)point;
  return NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
  (void)range;
  (void)actualRange;
  NSWindow* w = self.window;
  return w ? [w convertRectToScreen:w.frame] : NSZeroRect;
}

@end

@implementation LambdaPopoverDelegate

- (void)popoverDidClose:(NSNotification*)notification {
  (void)notification;
  lambda::MacMetalWindow* platform = self.platform;
  if (platform) {
    platform->handlePopoverClosed(lambda::PopoverSurfaceId{self.popoverId});
  }
}

@end

@implementation LambdaWindowDelegate

- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  lambda::MacMetalWindow* platform = self.platform;
  if (!platform) {
    return;
  }
  lambda::Window* w = platform->lambdaWindow();
  if (!w) {
    return;
  }
  void (^block)(void) = ^{
    lambda::Application::instance().eventQueue().post(lambda::WindowEvent{lambda::WindowEvent::Kind::CloseRequest,
                                                                        w->handle(), lambda::Size{}, 1.0f});
  };
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_async(dispatch_get_main_queue(), block);
  }
}

- (void)windowDidResize:(NSNotification*)notification {
  lambda::MacMetalWindow* platform = self.platform;
  if (!platform) {
    return;
  }
  lambda::Window* fw = platform->lambdaWindow();
  if (!fw) {
    return;
  }
  lambda::Size const currentSize = platform->currentSize();
  platform->positionNativeWindowControls();
  lambda::Application::instance().eventQueue().post(
      lambda::WindowEvent{lambda::WindowEvent::Kind::Resize, fw->handle(), currentSize, 1.0f});
  // Live resize runs in NSEventTrackingRunLoopMode; our main loop waits in NSDefaultRunLoopMode, so it does not
  // run the redraw pass until tracking ends. Dispatch + flush presents immediately during the drag.
  lambda::Application::instance().eventQueue().dispatch();
  // `flushRedraw` only presents when `requestRedraw` has been set. Declarative windows get this from
  // `Runtime`'s resize subscription; imperative apps must not rely on that — always request here.
  lambda::Application::instance().requestRedraw();
  fw->canvas().resize(static_cast<int>(std::lround(currentSize.width)),
                      static_cast<int>(std::lround(currentSize.height)));
  platform->setMetalLayerPresentsWithTransaction(true);
  lambda::setSyncPresentForCanvas(&fw->canvas(), true);
  lambda::Application::instance().flushRedraw();
  platform->setMetalLayerPresentsWithTransaction(false);
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
  (void)notification;
  lambda::MacMetalWindow* platform = self.platform;
  if (!platform) {
    return;
  }
  lambda::Window* fw = platform->lambdaWindow();
  if (!fw) {
    return;
  }
  lambda::Application::instance().eventQueue().post(
      lambda::WindowEvent{lambda::WindowEvent::Kind::FocusGained, fw->handle(), {}, 1.0f});
  lambda::Application::instance().requestRedraw();
}

- (void)windowDidResignKey:(NSNotification*)notification {
  (void)notification;
  lambda::MacMetalWindow* platform = self.platform;
  if (!platform) {
    return;
  }
  lambda::Window* fw = platform->lambdaWindow();
  if (!fw) {
    return;
  }
  lambda::Application::instance().eventQueue().post(
      lambda::WindowEvent{lambda::WindowEvent::Kind::FocusLost, fw->handle(), {}, 1.0f});
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
  NSWindow* win = static_cast<NSWindow*>(notification.object);
  lambda::MacMetalWindow* platform = self.platform;
  if (!platform || !platform->lambdaWindow()) {
    return;
  }
  CGFloat scale = win ? win.backingScaleFactor : 1.0;
  lambda::Window* fw = platform->lambdaWindow();
  lambda::Application::instance().eventQueue().post(lambda::WindowEvent{lambda::WindowEvent::Kind::DpiChanged,
                                                       fw->handle(), {}, static_cast<float>(scale)});
}

@end

@implementation LambdaMetalView (LambdaInput)

- (void)mouseDown:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerDown, event);
}

- (void)mouseUp:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerUp, event);
}

- (void)mouseMoved:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerMove, event);
}

- (void)mouseEntered:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerEnter, event);
}

- (void)mouseExited:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerLeave, event);
}

- (void)mouseDragged:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerMove, event);
}

- (void)rightMouseDown:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerDown, event);
}

- (void)rightMouseUp:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerUp, event);
}

- (void)otherMouseDown:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerDown, event);
}

- (void)otherMouseUp:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::PointerUp, event);
}

- (void)scrollWheel:(NSEvent*)event {
  lambda::detail::postInputFromView(self, lambda::InputEvent::Kind::Scroll, event);
}

@end

namespace lambda {

::lambda::Window* lambdaWindowForPlatform(MacMetalWindow* platform) {
  return platform ? platform->lambdaWindow() : nullptr;
}

CVReturn lambdaHandleDisplayLinkTick(MacMetalWindow* platform) {
  if (!platform) {
    return kCVReturnSuccess;
  }
  return platform->onDisplayLinkTick();
}

::lambda::Window* MacMetalWindow::lambdaWindow() const {
  return d ? d->lambdaWindow_ : nullptr;
}

namespace {

constexpr CGFloat kLambdaTitlebarHeight = 48.0;
constexpr CGFloat kNativeControlReservePadding = 8.0;

void setStandardWindowButtonsHidden(NSWindow* window, BOOL hidden) {
  if (!window) {
    return;
  }
  NSWindowButton const buttons[] = {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
  for (NSWindowButton buttonType : buttons) {
    NSButton* button = [window standardWindowButton:buttonType];
    if (button) {
      [button setHidden:hidden];
    }
  }
}

} // namespace

void MacMetalWindow::applyTitlebarMode() {
  if (!d || !d->window_) {
    return;
  }

  if (d->titlebarMode_ == WindowTitlebarMode::System) {
    [d->window_ setTitleVisibility:NSWindowTitleVisible];
    [d->window_ setTitlebarAppearsTransparent:NO];
    [d->window_ setMovableByWindowBackground:NO];
    setStandardWindowButtonsHidden(d->window_, NO);
    return;
  }

  [d->window_ setStyleMask:([d->window_ styleMask] | NSWindowStyleMaskFullSizeContentView)];
  [d->window_ setTitleVisibility:NSWindowTitleHidden];
  [d->window_ setTitlebarAppearsTransparent:YES];
  [d->window_ setMovableByWindowBackground:NO];
  if (@available(macOS 11.0, *)) {
    [d->window_ setTitlebarSeparatorStyle:NSTitlebarSeparatorStyleNone];
  }
  setStandardWindowButtonsHidden(d->window_,
                                 d->titlebarMode_ == WindowTitlebarMode::Client ||
                                     d->titlebarMode_ == WindowTitlebarMode::None);
  positionNativeWindowControls();
}

void MacMetalWindow::positionNativeWindowControls() {
  if (!d || !d->window_ || !d->metalView_ ||
      d->titlebarMode_ != WindowTitlebarMode::Integrated) {
    return;
  }

  NSView* contentView = d->metalView_;
  NSWindowButton const buttons[] = {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
  for (NSWindowButton buttonType : buttons) {
    NSButton* button = [d->window_ standardWindowButton:buttonType];
    NSView* buttonSuperview = button ? [button superview] : nil;
    if (!button || !buttonSuperview || [button isHidden]) {
      continue;
    }

    NSRect const contentRect = [contentView convertRect:[button frame] fromView:buttonSuperview];
    NSPoint const desiredCenterInContent =
        NSMakePoint(NSMidX(contentRect), kLambdaTitlebarHeight * 0.5);
    NSPoint const desiredCenterInSuperview =
        [buttonSuperview convertPoint:desiredCenterInContent fromView:contentView];

    NSRect frame = [button frame];
    frame.origin.y = desiredCenterInSuperview.y - frame.size.height * 0.5;
    [button setFrame:frame];
  }
}

void MacMetalWindow::applyBackground() {
  if (!d || !d->window_ || !d->metalView_) {
    return;
  }

  bool const wantsGlass = d->background_.kind == WindowBackgroundKind::Glass;
  if (wantsGlass) {
    [d->window_ setOpaque:NO];
    [d->window_ setBackgroundColor:[NSColor clearColor]];
    [d->window_ setHasShadow:YES];
    [d->window_ setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameVibrantLight]];

    if (!d->materialView_) {
      NSView* currentContent = [d->window_ contentView];
      NSRect bounds = currentContent ? [currentContent bounds] : [d->metalView_ bounds];
      d->materialView_ = [[NSVisualEffectView alloc] initWithFrame:bounds];
      d->materialView_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
      [d->metalView_ removeFromSuperview];
      [d->window_ setContentView:d->materialView_];
      d->metalView_.frame = d->materialView_.bounds;
    }

    d->materialView_.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    d->materialView_.state = NSVisualEffectStateActive;
    d->materialView_.material = NSVisualEffectMaterialPopover;
    d->materialView_.emphasized = NO;

    if (!d->tintView_) {
      d->tintView_ = [[NSView alloc] initWithFrame:d->materialView_.bounds];
      d->tintView_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
      d->tintView_.wantsLayer = YES;
      d->tintView_.layer.opaque = NO;
      [d->materialView_ addSubview:d->tintView_];
    }
    d->tintView_.frame = d->materialView_.bounds;
    Color tint = d->background_.glass.tintColor;
    tint.a *= std::clamp(d->background_.glass.opacity, 0.f, 1.f);
    d->tintView_.layer.backgroundColor =
        [[NSColor colorWithCalibratedRed:std::clamp(tint.r, 0.f, 1.f)
                                   green:std::clamp(tint.g, 0.f, 1.f)
                                    blue:std::clamp(tint.b, 0.f, 1.f)
                                   alpha:std::clamp(tint.a, 0.f, 1.f)] CGColor];
    [d->metalView_ removeFromSuperview];
    d->metalView_.frame = d->materialView_.bounds;
    [d->materialView_ addSubview:d->metalView_
                       positioned:NSWindowAbove
                       relativeTo:d->tintView_];
  } else {
    if (d->tintView_) {
      [d->tintView_ removeFromSuperview];
      d->tintView_ = nil;
    }
    if (d->materialView_) {
      [d->metalView_ removeFromSuperview];
      NSRect bounds = d->materialView_.bounds;
      d->metalView_.frame = bounds;
      [d->window_ setContentView:d->metalView_];
      d->materialView_ = nil;
    }
    [d->window_ setOpaque:d->background_.kind != WindowBackgroundKind::Transparent];
    [d->window_ setBackgroundColor:d->background_.kind == WindowBackgroundKind::Transparent
                                      ? [NSColor clearColor]
                                      : [NSColor windowBackgroundColor]];
  }
  positionNativeWindowControls();
}

MacMetalWindow::MacMetalWindow(const WindowConfig& config) : d(std::make_unique<Impl>()) {
  d->handle_ = gNextHandle.fetch_add(1, std::memory_order_relaxed);
  d->lambdaWindow_ = nullptr;
  d->window_ = nil;
  d->metalView_ = nil;
  d->materialView_ = nil;
  d->delegate_ = nil;
  d->titlebarMode_ = config.titlebar;

  NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
  if (config.resizable) {
    styleMask |= NSWindowStyleMaskResizable;
  }
  if (config.titlebar != WindowTitlebarMode::System) {
    styleMask |= NSWindowStyleMaskFullSizeContentView;
  }

  NSScreen* screen = [NSScreen mainScreen];
  NSRect visible = screen.visibleFrame;
  NSSize size = NSMakeSize(static_cast<CGFloat>(config.size.width), static_cast<CGFloat>(config.size.height));
  CGFloat x = visible.origin.x + (visible.size.width - size.width) * 0.5;
  CGFloat y = visible.origin.y + (visible.size.height - size.height) * 0.5;
  NSRect contentRect = NSMakeRect(x, y, size.width, size.height);

  d->window_ = [[NSWindow alloc] initWithContentRect:contentRect
                                          styleMask:styleMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
  applyTitlebarMode();
  [d->window_ setReleasedWhenClosed:NO];
  // Lambda owns cursor state. Stops _NSTrackingAreaAKManager from running its
  // cursor logic on this window and clobbering our setCursor decisions.
  [d->window_ disableCursorRects];
  if (config.minSize.width > 0.f || config.minSize.height > 0.f) {
    [d->window_ setContentMinSize:NSMakeSize(static_cast<CGFloat>(config.minSize.width),
                                             static_cast<CGFloat>(config.minSize.height))];
  }
  if (config.maxSize.width > 0.f || config.maxSize.height > 0.f) {
    CGFloat const maxW = config.maxSize.width > 0.f ? static_cast<CGFloat>(config.maxSize.width) : CGFLOAT_MAX;
    CGFloat const maxH = config.maxSize.height > 0.f ? static_cast<CGFloat>(config.maxSize.height) : CGFLOAT_MAX;
    [d->window_ setContentMaxSize:NSMakeSize(maxW, maxH)];
  }
  // Avoid scaling a stale snapshot during live resize; custom Metal content must update each frame.
  d->window_.preservesContentDuringLiveResize = NO;

  d->metalView_ = [[LambdaMetalView alloc] initWithFrame:[[d->window_ contentView] bounds]];
  d->metalView_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  d->metalView_.lambdaPlatform = this;
  [d->window_ setContentView:d->metalView_];
  applyBackground();
  positionNativeWindowControls();

  NSString* title = [NSString stringWithUTF8String:config.title.c_str()];
  if (!title) {
    title = @"";
  }
  [d->window_ setTitle:title];

  d->delegate_ = [[LambdaWindowDelegate alloc] init];
  d->delegate_.platform = this;
  [d->window_ setDelegate:d->delegate_];

  if (config.fullscreen) {
    NSWindow* w = d->window_;
    dispatch_async(dispatch_get_main_queue(), ^{
      [w toggleFullScreen:nil];
    });
  }
  if (@available(macOS 14.0, *)) {
    d->displayLink_ = [d->metalView_ displayLinkWithTarget:d->metalView_
                                                  selector:@selector(lambdaHandleDisplayLink:)];
    if (d->displayLink_) {
      [d->displayLink_ addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
      [d->displayLink_ setPaused:YES];
    }
  }
  if (!d->displayLink_) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CVDisplayLinkCreateWithActiveCGDisplays(&d->legacyDisplayLink_);
    if (d->legacyDisplayLink_) {
      CVDisplayLinkSetOutputCallback(d->legacyDisplayLink_, displayLinkOutputCallback, this);
    }
#pragma clang diagnostic pop
  }
  // `makeKeyAndOrderFront` is deferred to `show()` so `windowDidBecomeKey` runs after `setLambdaWindow`.
}

MacMetalWindow::~MacMetalWindow() {
  if (d && d->displayLink_) {
    [d->displayLink_ invalidate];
    d->displayLink_ = nil;
  }
  if (d && d->legacyDisplayLink_) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CVDisplayLinkStop(d->legacyDisplayLink_);
    CVDisplayLinkRelease(d->legacyDisplayLink_);
#pragma clang diagnostic pop
    d->legacyDisplayLink_ = nullptr;
  }
  if (d && d->delegate_) {
    d->delegate_.platform = nullptr;
  }
  if (d && d->window_) {
    [d->window_ setDelegate:nil];
  }
  if (d) {
    if (d->metalView_) {
      d->metalView_.lambdaPlatform = nullptr;
    }
    d->delegate_ = nil;
    d->metalView_ = nil;
    d->tintView_ = nil;
    d->materialView_ = nil;
    d->window_ = nil;
  }
  d.reset();
}

void MacMetalWindow::setLambdaWindow(::lambda::Window* window) {
  d->lambdaWindow_ = window;
}

void MacMetalWindow::show() {
  if (!d->window_ || !d->metalView_) {
    return;
  }
  positionNativeWindowControls();
  [d->window_ makeKeyAndOrderFront:nil];
  [d->window_ makeFirstResponder:d->metalView_];
}

void MacMetalWindow::resize(const Size& newSize) {
  if (!d->window_) {
    return;
  }
  NSSize sz = NSMakeSize(static_cast<CGFloat>(newSize.width), static_cast<CGFloat>(newSize.height));
  [d->window_ setContentSize:sz];
  positionNativeWindowControls();
}

void MacMetalWindow::setMinSize(Size size) {
  if (!d->window_) {
    return;
  }
  [d->window_ setContentMinSize:NSMakeSize(static_cast<CGFloat>(std::max(0.f, size.width)),
                                           static_cast<CGFloat>(std::max(0.f, size.height)))];
}

void MacMetalWindow::setMaxSize(Size size) {
  if (!d->window_) {
    return;
  }
  CGFloat const maxW = size.width > 0.f ? static_cast<CGFloat>(size.width) : CGFLOAT_MAX;
  CGFloat const maxH = size.height > 0.f ? static_cast<CGFloat>(size.height) : CGFLOAT_MAX;
  [d->window_ setContentMaxSize:NSMakeSize(maxW, maxH)];
}

void MacMetalWindow::setFullscreen(bool fullscreen) {
  if (!d->window_) {
    return;
  }
  const bool isFs = ([d->window_ styleMask] & NSWindowStyleMaskFullScreen) != 0;
  if (fullscreen == isFs) {
    return;
  }
  [d->window_ toggleFullScreen:nil];
}

void MacMetalWindow::setTitle(const std::string& title) {
  if (!d->window_) {
    return;
  }
  NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
  if (!nsTitle) {
    nsTitle = @"";
  }
  [d->window_ setTitle:nsTitle];
}

void MacMetalWindow::setTitlebarMode(WindowTitlebarMode mode) {
  if (d->titlebarMode_ == mode) {
    return;
  }
  d->titlebarMode_ = mode;
  applyTitlebarMode();
}

WindowTitlebarMode MacMetalWindow::titlebarMode() const {
  return d ? d->titlebarMode_ : WindowTitlebarMode::System;
}

void MacMetalWindow::setBackground(WindowBackground const& background) {
  if (!d) return;
  d->background_ = background;
  applyBackground();
}

WindowChromeMetrics MacMetalWindow::chromeMetrics() const {
  WindowChromeMetrics metrics{};
  if (!d || !d->window_) {
    return metrics;
  }
  metrics.titlebarMode = d->titlebarMode_;
  metrics.active = [d->window_ isKeyWindow];
  if (d->titlebarMode_ == WindowTitlebarMode::System) {
    return metrics;
  }

  metrics.titlebarHeight = static_cast<float>(kLambdaTitlebarHeight);
  metrics.systemControlsVisible = d->titlebarMode_ == WindowTitlebarMode::Integrated;
  if (!metrics.systemControlsVisible || !d->metalView_) {
    return metrics;
  }

  NSView* contentView = d->metalView_;
  bool hasRect = false;
  NSRect reserved = NSZeroRect;
  NSWindowButton const buttons[] = {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
  for (NSWindowButton buttonType : buttons) {
    NSButton* button = [d->window_ standardWindowButton:buttonType];
    if (!button || [button isHidden] || ![button superview]) {
      continue;
    }
    NSRect rect = [contentView convertRect:[button frame] fromView:[button superview]];
    reserved = hasRect ? NSUnionRect(reserved, rect) : rect;
    hasRect = true;
  }
  if (hasRect) {
    reserved = NSInsetRect(reserved, -kNativeControlReservePadding, -kNativeControlReservePadding);
    metrics.reservedRegions.push_back(Rect::sharp(
        static_cast<float>(std::max<CGFloat>(0.0, reserved.origin.x)),
        static_cast<float>(std::max<CGFloat>(0.0, reserved.origin.y)),
        static_cast<float>(std::max<CGFloat>(0.0, reserved.size.width)),
        static_cast<float>(std::max<CGFloat>(0.0, reserved.size.height))));
  }
  return metrics;
}

void MacMetalWindow::rememberPointerDownEvent(NSEvent* event) {
  if (!d) {
    return;
  }
  d->lastPointerDownEvent_ = event;
}

void MacMetalWindow::beginWindowDrag(std::uint32_t platformSerial) {
  (void)platformSerial;
  if (!d || !d->window_ || !d->lastPointerDownEvent_) {
    return;
  }
  [d->window_ performWindowDragWithEvent:d->lastPointerDownEvent_];
}

void MacMetalWindow::beginWindowResize(WindowResizeEdge edge, std::uint32_t platformSerial) {
  (void)edge;
  (void)platformSerial;
}

bool MacMetalWindow::showPopupMenu(PopupMenu menu, Rect anchor, std::uint32_t platformSerial) {
  (void)platformSerial;
  if (!d || !d->metalView_ || menu.items.empty()) {
    return false;
  }

  LambdaPopupMenuTarget* target = [[LambdaPopupMenuTarget alloc] init];
  target->lambdaWindow = d->lambdaWindow_;
  NSMenu* nsMenu = [[NSMenu alloc] initWithTitle:@""];
  for (MenuItem const& item : menu.items) {
    addPopupMenuItem(nsMenu, item, target);
  }
  if (nsMenu.numberOfItems == 0) {
    return false;
  }

  CGFloat const x = static_cast<CGFloat>(std::max(0.f, anchor.x));
  CGFloat const y = static_cast<CGFloat>(std::max(0.f, anchor.y + anchor.height + 4.f));
  [nsMenu popUpMenuPositioningItem:nil atLocation:NSMakePoint(x, y) inView:d->metalView_];
  return true;
}

PopoverSurfaceId MacMetalWindow::showPopover(Popover popover, Rect anchor, std::uint32_t platformSerial) {
  (void)platformSerial;
  if (!d || !d->metalView_ || !d->lambdaWindow_ || !d->window_ || ![d->window_ isVisible]) {
    return kInvalidPopoverSurfaceId;
  }
  PopoverSurfaceId const id{d->nextPopoverId_++};
  auto surface = std::make_unique<MacPopoverSurface>(this, id, std::move(popover));
  if (!surface->show(d->metalView_, anchor)) {
    return kInvalidPopoverSurfaceId;
  }
  d->popovers_.push_back(std::move(surface));
  return id;
}

void MacMetalWindow::repositionPopover(PopoverSurfaceId id, Popover const& popover, Rect anchor) {
  if (!d || !id.isValid()) {
    return;
  }
  auto it = std::find_if(d->popovers_.begin(), d->popovers_.end(),
                         [&](std::unique_ptr<MacPopoverSurface> const& surface) {
                           return surface && surface->id() == id;
                         });
  if (it != d->popovers_.end()) {
    (*it)->reposition(popover, anchor);
  }
}

void MacMetalWindow::dismissPopover(PopoverSurfaceId id) {
  if (!d || !id.isValid()) {
    return;
  }
  auto it = std::find_if(d->popovers_.begin(), d->popovers_.end(),
                         [&](std::unique_ptr<MacPopoverSurface> const& surface) {
                           return surface && surface->id() == id;
                         });
  if (it == d->popovers_.end()) {
    return;
  }
  if ((*it)->dispatchingEvent()) {
    (*it)->requestCloseAfterEvent();
    return;
  }
  (*it)->close();
  d->popovers_.erase(it);
}

void MacMetalWindow::handlePopoverClosed(PopoverSurfaceId id) {
  if (!d || !id.isValid()) {
    return;
  }
  auto it = std::find_if(d->popovers_.begin(), d->popovers_.end(),
                         [&](std::unique_ptr<MacPopoverSurface> const& surface) {
                           return surface && surface->id() == id;
                         });
  if (it == d->popovers_.end()) {
    return;
  }
  if ((*it)->dispatchingEvent()) {
    (*it)->requestCloseAfterEvent();
    return;
  }
  (*it)->notifyNativeClosed();
  d->popovers_.erase(it);
}

Size MacMetalWindow::currentSize() const {
  if (!d->window_ || !d->metalView_) {
    return {};
  }
  NSRect bounds = d->metalView_.bounds;
  return Size{static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height)};
}

std::optional<Rect> MacMetalWindow::currentFrame() const {
  if (!d->window_) {
    return std::nullopt;
  }
  NSRect frame = [d->window_ frame];
  return Rect::sharp(static_cast<float>(frame.origin.x),
                     static_cast<float>(frame.origin.y),
                     static_cast<float>(frame.size.width),
                     static_cast<float>(frame.size.height));
}

void MacMetalWindow::setFrame(Rect frame) {
  if (!d->window_ || frame.width <= 0.f || frame.height <= 0.f) {
    return;
  }
  NSRect nsFrame = NSMakeRect(static_cast<CGFloat>(frame.x),
                              static_cast<CGFloat>(frame.y),
                              static_cast<CGFloat>(frame.width),
                              static_cast<CGFloat>(frame.height));
  NSPoint const center = NSMakePoint(NSMidX(nsFrame), NSMidY(nsFrame));
  bool onScreen = false;
  for (NSScreen* screen in [NSScreen screens]) {
    if (NSPointInRect(center, screen.visibleFrame)) {
      onScreen = true;
      break;
    }
  }
  if (!onScreen) {
    NSScreen* screen = [NSScreen mainScreen];
    NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, nsFrame.size.width, nsFrame.size.height);
    nsFrame.size.width = std::min(nsFrame.size.width, visible.size.width);
    nsFrame.size.height = std::min(nsFrame.size.height, visible.size.height);
    nsFrame.origin.x = visible.origin.x + (visible.size.width - nsFrame.size.width) * 0.5;
    nsFrame.origin.y = visible.origin.y + (visible.size.height - nsFrame.size.height) * 0.5;
  }
  [d->window_ setFrame:nsFrame display:NO];
}

bool MacMetalWindow::isFullscreen() const {
  if (!d->window_) {
    return false;
  }
  return ([d->window_ styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

unsigned int MacMetalWindow::handle() const {
  return d->handle_;
}

void* MacMetalWindow::nativeGraphicsSurface() const {
  if (!d->metalView_) {
    return nullptr;
  }
  return (__bridge void*)d->metalView_.layer;
}

void MacMetalWindow::setMetalLayerPresentsWithTransaction(bool enable) {
  if (!d->metalView_) {
    return;
  }
  CAMetalLayer* ml = [d->metalView_ lambdaMetalLayer];
  if (ml) {
    ml.presentsWithTransaction = enable ? YES : NO;
  }
}

std::unique_ptr<Canvas> MacMetalWindow::createCanvas(::lambda::Window& owner) {
  (void)owner;
  void* layerPtr = nativeGraphicsSurface();
  if (!layerPtr) {
    return nullptr;
  }
  if (CAMetalLayer* layer = (__bridge CAMetalLayer*)layerPtr) {
    layer.opaque = NO;
    layer.backgroundColor = [[NSColor clearColor] CGColor];
  }
  return createMetalCanvas(&owner, layerPtr, handle(), Application::instance().textSystem(), [] {
    Application::instance().requestRedraw();
  });
}

void MacMetalWindow::processEvents() {
  if (!d->window_) {
    return;
  }
  NSEvent* event = nil;
  while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES])) {
    [NSApp sendEvent:event];
  }
}

void MacMetalWindow::waitForEvents(int timeoutMs) {
  if (!d->window_) {
    return;
  }
  NSDate* until = (timeoutMs < 0) ? [NSDate distantFuture]
                                : [NSDate dateWithTimeIntervalSinceNow:timeoutMs / 1000.0];
  NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                      untilDate:until
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
  if (event) {
    [NSApp sendEvent:event];
  }
  while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                     untilDate:[NSDate distantPast]
                                        inMode:NSDefaultRunLoopMode
                                       dequeue:YES])) {
    [NSApp sendEvent:event];
  }
}

void MacMetalWindow::wakeEventLoop() {
  if (!NSApp) {
    return;
  }
  auto postWakeEvent = ^{
    NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                     location:NSZeroPoint
                                modifierFlags:0
                                    timestamp:0
                                 windowNumber:0
                                      context:nil
                                      subtype:0
                                        data1:0
                                        data2:0];
    [NSApp postEvent:ev atStart:NO];
  };
  if ([NSThread isMainThread]) {
    postWakeEvent();
  } else {
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopDefaultMode, postWakeEvent);
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, postWakeEvent);
  }
  CFRunLoopWakeUp(CFRunLoopGetMain());
}

void MacMetalWindow::requestAnimationFrame() {
  bool const wasRequested = d->frameRequested_.exchange(true, std::memory_order_acq_rel);
  if (wasRequested) {
    return;
  }
  if (d->displayLink_) {
    setModernDisplayLinkPaused(false);
    return;
  }
  if (!d->legacyDisplayLink_) {
    return;
  }
  if (!d->legacyDisplayLinkRunning_.exchange(true, std::memory_order_acq_rel)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CVDisplayLinkStart(d->legacyDisplayLink_);
#pragma clang diagnostic pop
  }
}

void MacMetalWindow::acknowledgeAnimationFrameTick() {
  d->frameEventQueued_.store(false, std::memory_order_release);
}

void MacMetalWindow::completeAnimationFrame(bool needsAnotherFrame) {
  d->frameRequested_.store(needsAnotherFrame, std::memory_order_release);
  if (d->displayLink_) {
    if (!needsAnotherFrame) {
      setModernDisplayLinkPaused(true);
    }
    return;
  }
  if (needsAnotherFrame || !d->legacyDisplayLink_) {
    return;
  }
  if (d->legacyDisplayLinkRunning_.exchange(false, std::memory_order_acq_rel)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CVDisplayLinkStop(d->legacyDisplayLink_);
#pragma clang diagnostic pop
  }
}

CVReturn MacMetalWindow::onDisplayLinkTick() {
  Reactive::detail::profile::frameBoundary();
  if (!d->frameRequested_.load(std::memory_order_acquire)) {
    return kCVReturnSuccess;
  }
  if (!Application::hasInstance()) {
    return kCVReturnSuccess;
  }
  bool expected = false;
  if (!d->frameEventQueued_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return kCVReturnSuccess;
  }
  FrameEvent event{};
  event.deadlineNanos = nowSteadyClockNanos();
  event.windowHandle = handle();
  auto dispatchAndFlush = ^{
    if (!Application::hasInstance()) {
      return;
    }
    Application& app = Application::instance();
    app.eventQueue().post(event);
    app.eventQueue().dispatch();
    app.flushRedraw();
  };
  if ([NSThread isMainThread]) {
    dispatchAndFlush();
    return kCVReturnSuccess;
  }
  CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, dispatchAndFlush);
  CFRunLoopWakeUp(CFRunLoopGetMain());
  return kCVReturnSuccess;
}

void MacMetalWindow::setModernDisplayLinkPaused(bool paused) {
  id link = d ? d->displayLink_ : nil;
  if (!link) {
    return;
  }
  void (^updatePausedState)(void) = ^{
    [link setPaused:paused ? YES : NO];
  };
  if ([NSThread isMainThread]) {
    updatePausedState();
    return;
  }
  CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, updatePausedState);
  CFRunLoopWakeUp(CFRunLoopGetMain());
}

void MacMetalWindow::setCursor(Cursor kind) {
  if (kind == d->currentCursor_) return;
  d->currentCursor_ = kind;

  // Lazily cached NSCursor objects per Cursor kind.
  static NSCursor* cache[11] = { nil };
  NSCursor* c = cache[(int)kind];
  if (!c) {
    switch (kind) {
    case Cursor::Inherit:    c = [NSCursor arrowCursor]; break;
    case Cursor::Arrow:      c = [NSCursor arrowCursor]; break;
    case Cursor::IBeam:      c = [NSCursor IBeamCursor]; break;
    case Cursor::Hand:       c = [NSCursor pointingHandCursor]; break;
    case Cursor::ResizeEW:   c = [NSCursor resizeLeftRightCursor]; break;
    case Cursor::ResizeNS:   c = [NSCursor resizeUpDownCursor]; break;
    case Cursor::ResizeNESW: c = [NSCursor _windowResizeNorthEastSouthWestCursor]; break;
    case Cursor::ResizeNWSE: c = [NSCursor _windowResizeNorthWestSouthEastCursor]; break;
    case Cursor::ResizeAll:  c = [NSCursor openHandCursor]; break;
    case Cursor::Crosshair:  c = [NSCursor crosshairCursor]; break;
    case Cursor::NotAllowed: c = [NSCursor operationNotAllowedCursor]; break;
    }
    cache[(int)kind] = c;  // these are shared singletons; safe to retain implicitly
  }
  if (c && [NSCursor currentCursor] != c) {
    [c set];
  }
}

PlatformWindowCapabilities MacMetalWindow::capabilities() const {
  return {
      .supportsWindowGlass = true,
  };
}

namespace platform {

std::unique_ptr<Window> createWindow(const WindowConfig& config) {
  return std::make_unique<MacMetalWindow>(config);
}

} // namespace platform

} // namespace lambda
