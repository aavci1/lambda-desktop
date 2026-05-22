#include "Compositor/WaylandServer.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <utility>

namespace flux::compositor {

WaylandServer::WaylandServer(WaylandOutputInfo output) : impl_(std::make_unique<Impl>(std::move(output))) {}

WaylandServer::~WaylandServer() = default;

char const* WaylandServer::socketName() const noexcept {
  return impl_->socketName();
}

int WaylandServer::eventFd() const noexcept {
  return impl_->eventFd();
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

std::optional<int> WaylandServer::snapPreviewWakeDelayMs() const {
  return impl_->snapPreviewWakeDelayMs();
}

CommandLauncherSnapshot WaylandServer::commandLauncher() const {
  return impl_->commandLauncher();
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

void WaylandServer::dispatch() {
  impl_->dispatch();
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

void WaylandServer::setPreferredScale(float scale) {
  impl_->setPreferredScale(scale);
}

void WaylandServer::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  impl_->updateAnimations(timeMs, animationsEnabled);
}

bool WaylandServer::hasActiveAnimations() const noexcept {
  return impl_->hasActiveAnimations();
}

bool WaylandServer::hasIdleInhibitors() const noexcept {
  return impl_->hasIdleInhibitors();
}

void WaylandServer::sendFrameCallbacksOnly(std::uint32_t timeMs) {
  impl_->sendFrameCallbacksOnly(timeMs);
}

void WaylandServer::sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing) {
  impl_->sendPresentationFeedbacks(timeMs, timing);
}

void WaylandServer::sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing) {
  impl_->sendFrameCallbacks(timeMs, timing);
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

} // namespace flux::compositor
