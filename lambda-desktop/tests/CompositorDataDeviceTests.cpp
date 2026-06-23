#include "Compositor/Wayland/DataDeviceDndState.hpp"
#include "Compositor/Wayland/SelectionDeviceSeatState.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <span>
#include <vector>

TEST_CASE("data-device DnD action masks validate protocol bits") {
  using namespace lambdaui::compositor;

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
  using namespace lambdaui::compositor;

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
  using namespace lambdaui::compositor;

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
  using namespace lambdaui::compositor;

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
  using namespace lambdaui::compositor;

  CHECK(dataDeviceCanUseDragIconSurface(false, false));
  CHECK(dataDeviceCanUseDragIconSurface(false, true));
  CHECK(dataDeviceCanUseDragIconSurface(true, true));
  CHECK_FALSE(dataDeviceCanUseDragIconSurface(true, false));
}

TEST_CASE("data-device drop cleanup preserves offers after completed drops") {
  using namespace lambdaui::compositor;

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
  using namespace lambdaui::compositor;

  CHECK(dataOfferShouldCancelSourceOnDestroy(true, true, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(false, true, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(true, false, false));
  CHECK_FALSE(dataOfferShouldCancelSourceOnDestroy(true, true, true));
}

TEST_CASE("selection devices clear destroyed seat resource references") {
  using namespace lambdaui::compositor;
  using WaylandServer = lambdaui::compositor::WaylandServer;

  auto* seat = reinterpret_cast<wl_resource*>(std::uintptr_t{1});
  auto* otherSeat = reinterpret_cast<wl_resource*>(std::uintptr_t{2});

  std::vector<std::unique_ptr<WaylandServer::Impl::DataDevice>> dataDevices;
  dataDevices.push_back(std::make_unique<WaylandServer::Impl::DataDevice>());
  dataDevices.push_back(std::make_unique<WaylandServer::Impl::DataDevice>());
  dataDevices[0]->seat = seat;
  dataDevices[1]->seat = otherSeat;

  std::vector<std::unique_ptr<WaylandServer::Impl::PrimarySelectionDevice>> primarySelectionDevices;
  primarySelectionDevices.push_back(std::make_unique<WaylandServer::Impl::PrimarySelectionDevice>());
  primarySelectionDevices.push_back(std::make_unique<WaylandServer::Impl::PrimarySelectionDevice>());
  primarySelectionDevices[0]->seat = seat;
  primarySelectionDevices[1]->seat = otherSeat;

  CHECK(dataDeviceReferencesSeatResource(dataDevices[0].get(), seat));
  CHECK(primarySelectionDeviceReferencesSeatResource(primarySelectionDevices[0].get(), seat));

  auto clearSeat = [&](wl_resource* seatResource) {
    return clearSelectionDeviceSeatResources(
        std::span<std::unique_ptr<WaylandServer::Impl::DataDevice>>(dataDevices.data(), dataDevices.size()),
        std::span<std::unique_ptr<WaylandServer::Impl::PrimarySelectionDevice>>(
            primarySelectionDevices.data(),
            primarySelectionDevices.size()),
        seatResource);
  };

  SelectionDeviceSeatCleanup nullCleanup = clearSeat(nullptr);
  CHECK_FALSE(nullCleanup.changed());
  CHECK(dataDevices[0]->seat == seat);
  CHECK(primarySelectionDevices[0]->seat == seat);

  SelectionDeviceSeatCleanup otherCleanup = clearSeat(otherSeat);
  CHECK(otherCleanup.dataDevicesCleared == 1);
  CHECK(otherCleanup.primarySelectionDevicesCleared == 1);
  CHECK(dataDevices[0]->seat == seat);
  CHECK(dataDevices[1]->seat == nullptr);
  CHECK(primarySelectionDevices[0]->seat == seat);
  CHECK(primarySelectionDevices[1]->seat == nullptr);

  SelectionDeviceSeatCleanup cleanup = clearSeat(seat);
  CHECK(cleanup.dataDevicesCleared == 1);
  CHECK(cleanup.primarySelectionDevicesCleared == 1);
  CHECK(dataDevices[0]->seat == nullptr);
  CHECK(primarySelectionDevices[0]->seat == nullptr);
}
