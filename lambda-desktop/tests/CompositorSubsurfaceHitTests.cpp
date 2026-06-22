#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <doctest/doctest.h>

#include <utility>
#include <vector>

namespace {

bool containsPoint(float x, float y, float left, float top, float right, float bottom) {
  return x >= left && x < right && y >= top && y < bottom;
}

bool subsurfaceTreeContains(float x,
                            float y,
                            float parentX,
                            float parentY,
                            float subsurfaceX,
                            float subsurfaceY,
                            float subsurfaceWidth,
                            float subsurfaceHeight) {
  float const left = parentX + subsurfaceX;
  float const top = parentY + subsurfaceY;
  return containsPoint(x, y, left, top, left + subsurfaceWidth, top + subsurfaceHeight);
}

} // namespace

TEST_CASE("subsurface hit testing uses parent offset and child size") {
  CHECK(subsurfaceTreeContains(130.f, 80.f, 100.f, 50.f, 10.f, 20.f, 40.f, 30.f));
  CHECK_FALSE(subsurfaceTreeContains(105.f, 80.f, 100.f, 50.f, 10.f, 20.f, 40.f, 30.f));
  CHECK_FALSE(subsurfaceTreeContains(130.f, 65.f, 100.f, 50.f, 10.f, 20.f, 40.f, 30.f));
}

TEST_CASE("nested subsurface coordinates accumulate") {
  float const nestedLeft = 10.f + 5.f;
  float const nestedTop = 10.f + 5.f;
  CHECK(containsPoint(21.f, 21.f, nestedLeft, nestedTop, nestedLeft + 20.f, nestedTop + 20.f));
}

TEST_CASE("subsurface position is pending until the subsurface commits") {
  using namespace lambda::compositor;

  WaylandServer::Impl::Subsurface subsurface;
  subsurface.x = 4;
  subsurface.y = 5;
  subsurface.pendingX = 40;
  subsurface.pendingY = 50;

  CHECK(subsurface.x == 4);
  CHECK(subsurface.y == 5);

  CHECK(applySubsurfacePendingPosition(&subsurface));
  CHECK(subsurface.x == 40);
  CHECK(subsurface.y == 50);
  CHECK_FALSE(applySubsurfacePendingPosition(&subsurface));
}

TEST_CASE("subsurface sibling placement is pending until parent commit") {
  using namespace lambda::compositor;

  WaylandServer::Impl::Surface parent;
  WaylandServer::Impl::Surface childSurfaceA;
  WaylandServer::Impl::Surface childSurfaceB;
  WaylandServer::Impl::Surface childSurfaceC;
  WaylandServer::Impl::Subsurface childA;
  WaylandServer::Impl::Subsurface childB;
  WaylandServer::Impl::Subsurface childC;
  childA.parent = &parent;
  childA.surface = &childSurfaceA;
  childA.order = 1;
  childA.pendingOrder = 1;
  childB.parent = &parent;
  childB.surface = &childSurfaceB;
  childB.order = 2;
  childB.pendingOrder = 2;
  childC.parent = &parent;
  childC.surface = &childSurfaceC;
  childC.order = 3;
  childC.pendingOrder = 3;
  std::vector<WaylandServer::Impl::Subsurface*> mutableChildren{&childA, &childB, &childC};
  std::vector<WaylandServer::Impl::Subsurface const*> children{&childA, &childB, &childC};

  REQUIRE(setSubsurfacePendingPlaceBelow(mutableChildren, &childC, &childSurfaceA));
  auto currentOrder = orderedSubsurfacesForParent(children, &parent, SubsurfaceStackLayer::Above);
  REQUIRE(currentOrder.size() == 3);
  CHECK(currentOrder[0] == &childA);
  CHECK(currentOrder[1] == &childB);
  CHECK(currentOrder[2] == &childC);

  CHECK(childC.pendingOrder < childA.pendingOrder);
  CHECK(applySubsurfacePendingOrder(mutableChildren, &parent));
  currentOrder = orderedSubsurfacesForParent(children, &parent, SubsurfaceStackLayer::Above);
  REQUIRE(currentOrder.size() == 3);
  CHECK(currentOrder[0] == &childC);
  CHECK(currentOrder[1] == &childA);
  CHECK(currentOrder[2] == &childB);
}

TEST_CASE("subsurface synchronization follows ancestor chain") {
  using namespace lambda::compositor;

  WaylandServer::Impl::Surface parentSurface;
  WaylandServer::Impl::Surface childSurface;
  WaylandServer::Impl::Subsurface parentRole;
  WaylandServer::Impl::Subsurface childRole;
  parentSurface.role = SurfaceRole::Subsurface;
  parentSurface.subsurfaceRole = &parentRole;
  parentRole.surface = &parentSurface;
  parentRole.synchronized = true;
  childRole.surface = &childSurface;
  childRole.parent = &parentSurface;
  childRole.synchronized = false;

  CHECK(subsurfaceIsEffectivelySynchronized(&parentRole));
  CHECK(subsurfaceIsEffectivelySynchronized(&childRole));

  parentRole.synchronized = false;
  CHECK_FALSE(subsurfaceIsEffectivelySynchronized(&childRole));
}

TEST_CASE("synchronized subsurface commit caches pending surface state") {
  using namespace lambda::compositor;

  WaylandServer::Impl::Surface surface;
  WaylandServer::Impl::Subsurface subsurface;
  surface.subsurfaceRole = &subsurface;
  subsurface.surface = &surface;
  subsurface.synchronized = true;
  surface.pendingBufferState.scale = 2;
  surface.pendingBufferState.scaleSet = true;
  surface.pendingViewportState.destinationSet = true;
  surface.pendingViewportState.destinationWidth = 120;
  surface.pendingViewportState.destinationHeight = 80;
  surface.pendingDamageState.surfaceRects.push_back({.x = 0, .y = 0, .width = 10, .height = 10});
  surface.pendingFrameCallbacks.push_back(nullptr);

  CHECK(cacheSynchronizedSubsurfaceCommit(&surface));
  REQUIRE(surface.cachedSubsurfaceCommit.has_value());
  CHECK_FALSE(surface.pendingBufferState.scaleSet);
  CHECK_FALSE(surface.pendingViewportState.destinationSet);
  CHECK(surface.pendingDamageState.surfaceRects.empty());
  CHECK(surface.pendingFrameCallbacks.empty());

  surface.pendingBufferState.transform = WL_OUTPUT_TRANSFORM_90;
  surface.pendingBufferState.transformSet = true;
  auto livePending = takeSurfacePendingCommit(&surface);
  REQUIRE(restoreCachedSubsurfaceCommit(&surface));
  CHECK(surface.pendingBufferState.scaleSet);
  CHECK(surface.pendingBufferState.scale == 2);
  CHECK(surface.pendingViewportState.destinationSet);
  CHECK(surface.pendingViewportState.destinationWidth == 120);
  CHECK(surface.pendingDamageState.surfaceRects.size() == 1);
  CHECK(surface.pendingFrameCallbacks.size() == 1);

  restoreSurfacePendingCommit(&surface, std::move(livePending));
  CHECK(surface.pendingBufferState.transformSet);
  CHECK(surface.pendingBufferState.transform == WL_OUTPUT_TRANSFORM_90);
  CHECK_FALSE(surface.cachedSubsurfaceCommit.has_value());
}

TEST_CASE("subsurface commit caches while an ancestor is synchronized") {
  using namespace lambda::compositor;

  WaylandServer::Impl::Surface parentSurface;
  WaylandServer::Impl::Surface childSurface;
  WaylandServer::Impl::Subsurface parentRole;
  WaylandServer::Impl::Subsurface childRole;
  parentSurface.role = SurfaceRole::Subsurface;
  parentSurface.subsurfaceRole = &parentRole;
  parentRole.surface = &parentSurface;
  parentRole.synchronized = true;
  childSurface.subsurfaceRole = &childRole;
  childRole.surface = &childSurface;
  childRole.parent = &parentSurface;
  childRole.synchronized = false;
  childSurface.pendingBufferState.scale = 3;
  childSurface.pendingBufferState.scaleSet = true;

  CHECK(cacheSynchronizedSubsurfaceCommit(&childSurface));
  REQUIRE(childSurface.cachedSubsurfaceCommit.has_value());
  CHECK(childSurface.cachedSubsurfaceCommit->bufferState.scaleSet);
  CHECK(childSurface.cachedSubsurfaceCommit->bufferState.scale == 3);
  CHECK_FALSE(childSurface.pendingBufferState.scaleSet);
}
