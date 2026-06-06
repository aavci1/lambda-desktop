#include "Compositor/WaylandServer.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lambda::compositor {

WaylandServer::WaylandServer(WaylandOutputInfo output) : impl_(std::make_unique<Impl>(std::move(output))) {}

WaylandServer::~WaylandServer() = default;

char const* WaylandServer::socketName() const noexcept {
  return impl_->socketName();
}

int WaylandServer::eventFd() const noexcept {
  return impl_->eventFd();
}

int WaylandServer::shellIpcFd() const noexcept {
  return impl_->shellIpcFd();
}

float WaylandServer::preferredScale() const noexcept {
  return impl_->preferredScale();
}

std::int32_t WaylandServer::logicalOutputWidth() const noexcept {
  return impl_->logicalOutputWidth();
}

std::int32_t WaylandServer::logicalOutputHeight() const noexcept {
  return impl_->logicalOutputHeight();
}

std::size_t WaylandServer::toplevelCount() const noexcept {
  return impl_->toplevelCount();
}

std::vector<CommittedSurfaceSnapshot> WaylandServer::committedSurfaces() const {
  return impl_->committedSurfaces();
}

std::optional<CommittedSurfaceSnapshot> WaylandServer::cursorSurface() const {
  return impl_->cursorSurface();
}

std::optional<SnapPreviewSnapshot> WaylandServer::snapPreview() const {
  return impl_->snapPreview();
}

std::optional<WindowCyclerOverlaySnapshot> WaylandServer::windowCyclerOverlay() {
  return impl_->windowCyclerOverlay();
}

std::optional<int> WaylandServer::windowCyclerWakeDelayMs() const {
  return impl_->windowCyclerWakeDelayMs();
}

std::optional<int> WaylandServer::snapPreviewWakeDelayMs() const {
  return impl_->snapPreviewWakeDelayMs();
}

bool WaylandServer::hasPendingFrameCallbacks() const noexcept {
  return impl_->hasPendingFrameCallbacks();
}

std::uint64_t WaylandServer::contentSerial() const noexcept {
  return impl_->contentSerial_;
}

std::vector<int> WaylandServer::duplicateDmabufFds(std::uint64_t surfaceId) const {
  return impl_->duplicateDmabufFds(surfaceId);
}

bool WaylandServer::copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const {
  return impl_->copyDmabufToRgba(surfaceId, out);
}

void WaylandServer::consumeSurfaceDamage(std::uint64_t surfaceId, std::uint64_t serial) {
  impl_->consumeSurfaceDamage(surfaceId, serial);
}

void WaylandServer::dispatch() {
  impl_->dispatch();
}

void WaylandServer::dispatchShellIpc() {
  impl_->dispatchShellIpc();
}

std::optional<ScreenshotRequest> WaylandServer::consumeScreenshotRequest() {
  return impl_->consumeScreenshotRequest();
}

std::optional<ScreenshotSelectionOverlay> WaylandServer::screenshotSelectionOverlay() const {
  return impl_->screenshotSelectionOverlay();
}

void WaylandServer::notifyShellStateChanged() {
  impl_->notifyShellStateChanged();
}

void WaylandServer::flushClients() {
  impl_->flushClients();
}

void WaylandServer::setShortcutBindings(std::vector<ShortcutBinding> bindings) {
  impl_->setShortcutBindings(std::move(bindings));
}

void WaylandServer::setChromeConfig(ChromeConfig config) {
  impl_->setChromeConfig(config);
}

void WaylandServer::setChromeThemeConfig(ChromeConfig base, std::optional<ChromeConfig> dark) {
  impl_->setChromeThemeConfig(std::move(base), std::move(dark));
}

void WaylandServer::setShellThemeDark(bool dark) {
  impl_->setShellThemeDark(dark);
}

void WaylandServer::setInputConfig(CompositorInputConfig config) {
  impl_->setInputConfig(config);
}

void WaylandServer::setPreferredScale(float scale) {
  impl_->setPreferredScale(scale);
}

void WaylandServer::setDmabufFormatModifierPreferences(std::vector<DmabufFormatModifierPreference> preferences) {
  impl_->setDmabufFormatModifierPreferences(std::move(preferences));
}

void WaylandServer::setRetainedDmabufBufferIds(std::vector<std::uint64_t> bufferIds) {
  impl_->setRetainedDmabufBufferIds(std::move(bufferIds));
}

void WaylandServer::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  impl_->updateAnimations(timeMs, animationsEnabled);
}

bool WaylandServer::hasActiveAnimations() const noexcept {
  return impl_->hasActiveAnimations();
}

bool WaylandServer::hasActiveResizePacing() const noexcept {
  return impl_->hasActiveResizePacing();
}

bool WaylandServer::hasIdleInhibitors() const noexcept {
  return impl_->hasIdleInhibitors();
}

void WaylandServer::sendFrameCallbacksOnly(std::uint32_t timeMs) {
  impl_->sendFrameCallbacksOnly(timeMs);
}

void WaylandServer::sendFrameCallbacksOnly(std::uint32_t timeMs,
                                           std::span<std::uint64_t const> frameSurfaceIds) {
  impl_->sendFrameCallbacksOnly(timeMs, frameSurfaceIds);
}

void WaylandServer::sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing) {
  impl_->sendPresentationFeedbacks(timeMs, timing);
}

void WaylandServer::sendPresentationFeedbacks(std::uint32_t timeMs,
                                              PresentationTiming timing,
                                              std::span<std::uint64_t const> frameSurfaceIds) {
  impl_->sendPresentationFeedbacks(timeMs, timing, frameSurfaceIds);
}

void WaylandServer::sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing) {
  impl_->sendFrameCallbacks(timeMs, timing);
}

void WaylandServer::sendFrameCallbacks(std::uint32_t timeMs,
                                       PresentationTiming timing,
                                       std::span<std::uint64_t const> frameSurfaceIds) {
  impl_->sendFrameCallbacks(timeMs, timing, frameSurfaceIds);
}

void WaylandServer::completePresentationFeedbacks(std::vector<PresentationCompletion> const& completions,
                                                  std::uint32_t timeMs) {
  impl_->completePresentationFeedbacks(completions, timeMs);
}

void WaylandServer::handlePointerMotion(double dx, double dy, std::uint32_t timeMs) {
  impl_->handlePointerMotion(dx, dy, timeMs);
}

void WaylandServer::handlePointerPosition(double x, double y, std::uint32_t timeMs) {
  impl_->handlePointerPosition(x, y, timeMs);
}

void WaylandServer::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  impl_->handlePointerButton(button, pressed, timeMs);
}

void WaylandServer::handlePointerAxis(double dx, double dy, std::uint32_t timeMs) {
  impl_->handlePointerAxis(dx, dy, timeMs);
}

void WaylandServer::handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  impl_->handleKeyboardKey(key, pressed, timeMs);
}

void WaylandServer::resetKeyboardState(std::uint32_t timeMs) {
  impl_->resetKeyboardState(timeMs);
}

float WaylandServer::pointerX() const noexcept {
  return impl_->pointerX_;
}

float WaylandServer::pointerY() const noexcept {
  return impl_->pointerY_;
}

CursorShape WaylandServer::cursorShape() const noexcept {
  if (impl_->compositorCursorOverride_) return impl_->compositorCursorShape_;
  return impl_->cursorShape_;
}

bool WaylandServer::diagnosticExerciseTopToplevel(std::uint32_t step, bool resize) {
  auto* surface = wm::mostRecentToplevel(impl_.get());
  if (!wm::isManagedToplevel(surface)) return false;
  if (surface->fullscreen || surface->maximized || surface->snapped || surface->minimized) return false;

  std::int32_t const baseWidth = std::max(320, displayWidth(surface));
  std::int32_t const baseHeight = std::max(220, displayHeight(surface));
  std::int32_t const maxX = std::max(0, logicalOutputWidth() - baseWidth - 32);
  std::int32_t const maxY = std::max(0, logicalOutputHeight() - baseHeight - 32);
  float const t = static_cast<float>(step) * 0.105f;
  std::int32_t const nextX = std::clamp(32 + static_cast<std::int32_t>(std::lround((std::sin(t) * 0.5f + 0.5f) *
                                                                                  static_cast<float>(maxX))),
                                       0,
                                       std::max(0, logicalOutputWidth() - baseWidth));
  std::int32_t const nextY = std::clamp(48 + static_cast<std::int32_t>(std::lround((std::cos(t * 0.7f) * 0.5f + 0.5f) *
                                                                                  static_cast<float>(maxY))),
                                       0,
                                       std::max(0, logicalOutputHeight() - baseHeight));

  if (resize && step % 30u == 0u) {
    std::int32_t const nextWidth = std::clamp(620 + static_cast<std::int32_t>(std::lround((std::sin(t * 0.41f) * 0.5f + 0.5f) * 260.f)),
                                             360,
                                             std::max(360, logicalOutputWidth() - 96));
    std::int32_t const nextHeight = std::clamp(360 + static_cast<std::int32_t>(std::lround((std::cos(t * 0.37f) * 0.5f + 0.5f) * 180.f)),
                                              240,
                                              std::max(240, logicalOutputHeight() - 96));
    return requestToplevelResizeConfigure(impl_.get(), surface, nextX, nextY, nextWidth, nextHeight);
  }

  if (surface->windowX == nextX && surface->windowY == nextY) return false;
  surface->windowX = nextX;
  surface->windowY = nextY;
  surface->geometryAnimationActive = false;
  ++impl_->contentSerial_;
  return true;
}

} // namespace lambda::compositor
