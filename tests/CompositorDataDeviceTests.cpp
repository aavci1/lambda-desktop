#include "Compositor/Wayland/DataDeviceDndState.hpp"

#include <doctest/doctest.h>

TEST_CASE("data-device DnD action masks validate protocol bits") {
  using namespace lambda::compositor;

  CHECK(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE));
  CHECK(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE));
  CHECK(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK));
  CHECK(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                           WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                           WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK));
  CHECK_FALSE(validDndActionMask(0x80000000u));
  CHECK_FALSE(validDndActionMask(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | 0x80000000u));

  CHECK(validPreferredDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE));
  CHECK(validPreferredDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK(validPreferredDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE));
  CHECK(validPreferredDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK));
  CHECK_FALSE(validPreferredDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                      WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE));
  CHECK_FALSE(validPreferredDndAction(0x80000000u));
}

TEST_CASE("data-device DnD action negotiation honors preference then fallback order") {
  using namespace lambda::compositor;

  std::uint32_t const all = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                            WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                            WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  CHECK(chooseDndAction(all, all, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
  CHECK(chooseDndAction(all, all, WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK);
  CHECK(chooseDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                        WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE,
                        WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
  CHECK(chooseDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                            WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE,
                        all,
                        WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
  CHECK(chooseDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                            WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK,
                        all,
                        WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
  CHECK(chooseDndAction(WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK,
                        all,
                        WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) ==
        WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK);
}

TEST_CASE("data-device DnD offer request validation enforces DnD finish rules") {
  using namespace lambda::compositor;

  CHECK(dataOfferAcceptsDndActions(true));
  CHECK_FALSE(dataOfferAcceptsDndActions(false));

  CHECK(dataOfferCanFinishDnd(false, true, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK(dataOfferCanFinishDnd(false, true, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE));
  CHECK(dataOfferCanFinishDnd(false, true, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK));
  CHECK_FALSE(dataOfferCanFinishDnd(true, true, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK_FALSE(dataOfferCanFinishDnd(false, false, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK_FALSE(dataOfferCanFinishDnd(false, true, false, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY));
  CHECK_FALSE(dataOfferCanFinishDnd(false, true, true, WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE));

  CHECK(dataOfferCanProcessRequest(false));
  CHECK_FALSE(dataOfferCanProcessRequest(true));
}

TEST_CASE("data-device source lifecycle validation enforces DnD action ordering") {
  using namespace lambda::compositor;

  CHECK(dataSourceCanSetDndActions(false, false));
  CHECK_FALSE(dataSourceCanSetDndActions(true, false));
  CHECK_FALSE(dataSourceCanSetDndActions(false, true));
  CHECK_FALSE(dataSourceCanSetDndActions(true, true));

  CHECK(dataSourceCanStartDrag(false));
  CHECK_FALSE(dataSourceCanStartDrag(true));

  CHECK(dataSourceCanSetSelection(false, false));
  CHECK_FALSE(dataSourceCanSetSelection(true, false));
  CHECK_FALSE(dataSourceCanSetSelection(false, true));
  CHECK_FALSE(dataSourceCanSetSelection(true, true));
}

TEST_CASE("data-device drag icon validation requires an unassigned surface role") {
  using namespace lambda::compositor;

  CHECK(dataDeviceCanUseDragIconSurface(false, false));
  CHECK(dataDeviceCanUseDragIconSurface(false, true));
  CHECK(dataDeviceCanUseDragIconSurface(true, true));
  CHECK_FALSE(dataDeviceCanUseDragIconSurface(true, false));
}

TEST_CASE("data-device drop cleanup preserves offers after completed drops") {
  using namespace lambda::compositor;

  DndClearPlan const cancelled = dndClearPlanAfterDrop(false);
  CHECK(cancelled.destroyOffer);
  CHECK(cancelled.sendLeave);
  CHECK(cancelled.cancelSource);
  CHECK_FALSE(dndSourceShouldReceiveDropPerformed(false));

  DndClearPlan const completed = dndClearPlanAfterDrop(true);
  CHECK_FALSE(completed.destroyOffer);
  CHECK_FALSE(completed.sendLeave);
  CHECK_FALSE(completed.cancelSource);
  CHECK(dndSourceShouldReceiveDropPerformed(true));
}

TEST_CASE("data-device offer destruction cancels only unfinished completed drops") {
  using namespace lambda::compositor;

  CHECK(dataOfferShouldCancelSourceOnDestroy(true, true, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(false, true, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(true, false, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(true, true, true));
}
